#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "driver/i2s_pdm.h"
#include "esp_check.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "esp_afe_sr_iface.h"
#include "esp_afe_sr_models.h"
#include "esp_mn_iface.h"
#include "esp_mn_models.h"
#include "esp_mn_speech_commands.h"
#include "esp_process_sdkconfig.h"
#include "esp_wn_iface.h"
#include "esp_wn_models.h"
#include "model_path.h"

#define MIC_DATA_GPIO 41
#define MIC_CLK_GPIO 42
#define SAMPLE_RATE_HZ 16000
#define BITS_PER_SAMPLE 16

static const char *TAG = "cyd_voice";

static const esp_afe_sr_iface_t *s_afe = NULL;
static esp_afe_sr_data_t *s_afe_data = NULL;
static srmodel_list_t *s_models = NULL;
static i2s_chan_handle_t s_rx_chan = NULL;
static volatile bool s_running = true;
static volatile bool s_awake = false;

typedef struct {
    int command_id;
    const char *event_name;
    const char *phrase;
} command_event_t;

static const command_event_t COMMAND_EVENTS[] = {
    {1, "take_picture", "TAKE A PICTURE"},
    {2, "look_around", "LOOK AROUND"},
    {3, "what_do_you_see", "WHAT DO YOU SEE"},
    {4, "tell_joke", "TELL ME A JOKE"},
    {5, "say_something_rude", "SAY SOMETHING RUDE"},
    {6, "be_nice", "BE NICE"},
    {7, "be_sarcastic", "BE SARCASTIC"},
    {8, "be_quiet", "BE QUIET"},
    {9, "talk_more", "TALK MORE"},
    {10, "wake_up", "WAKE UP"},
    {11, "go_to_sleep", "GO TO SLEEP"},
    {12, "happy_mode", "HAPPY MODE"},
    {13, "sad_mode", "SAD MODE"},
    {14, "angry_mode", "ANGRY MODE"},
    {15, "suspicious_mode", "SUSPICIOUS MODE"},
    {16, "love_mode", "LOVE MODE"},
    {17, "remember_me", "REMEMBER ME"},
    {18, "forget_me", "FORGET ME"},
    {19, "connect_wifi", "CONNECT WIFI"},
    {20, "status", "STATUS"},
};

static const char *event_for_command(int command_id)
{
    for (size_t i = 0; i < sizeof(COMMAND_EVENTS) / sizeof(COMMAND_EVENTS[0]); i++) {
        if (COMMAND_EVENTS[i].command_id == command_id) {
            return COMMAND_EVENTS[i].event_name;
        }
    }
    return "unknown";
}

static void register_buddy_commands(void)
{
    ESP_ERROR_CHECK(esp_mn_commands_clear());
    for (size_t i = 0; i < sizeof(COMMAND_EVENTS) / sizeof(COMMAND_EVENTS[0]); i++) {
        ESP_LOGI(TAG, "voice cmd %d: %s -> %s",
                 COMMAND_EVENTS[i].command_id,
                 COMMAND_EVENTS[i].phrase,
                 COMMAND_EVENTS[i].event_name);
        ESP_ERROR_CHECK(esp_mn_commands_add(
            COMMAND_EVENTS[i].command_id,
            (char *)COMMAND_EVENTS[i].phrase));
    }
    esp_mn_error_t *command_errors = esp_mn_commands_update();
    if (command_errors) {
        ESP_LOGW(TAG, "Some voice commands were rejected by MultiNet");
    }
    esp_mn_commands_print();
    esp_mn_active_commands_print();
}

static esp_err_t init_pdm_mic(void)
{
    i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_AUTO, I2S_ROLE_MASTER);
    ESP_RETURN_ON_ERROR(i2s_new_channel(&chan_cfg, NULL, &s_rx_chan), TAG, "i2s_new_channel failed");

    i2s_pdm_rx_config_t pdm_rx_cfg = {
        .clk_cfg = I2S_PDM_RX_CLK_DEFAULT_CONFIG(SAMPLE_RATE_HZ),
        .slot_cfg = I2S_PDM_RX_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_MONO),
        .gpio_cfg = {
            .clk = MIC_CLK_GPIO,
            .din = MIC_DATA_GPIO,
            .invert_flags = {
                .clk_inv = false,
            },
        },
    };

    ESP_RETURN_ON_ERROR(i2s_channel_init_pdm_rx_mode(s_rx_chan, &pdm_rx_cfg), TAG, "pdm rx init failed");
    ESP_RETURN_ON_ERROR(i2s_channel_enable(s_rx_chan), TAG, "pdm rx enable failed");
    return ESP_OK;
}

static void feed_task(void *arg)
{
    esp_afe_sr_data_t *afe_data = (esp_afe_sr_data_t *)arg;
    const int audio_chunksize = s_afe->get_feed_chunksize(afe_data);
    const int feed_channels = s_afe->get_feed_channel_num(afe_data);
    ESP_LOGI(TAG, "feed chunksize=%d channels=%d", audio_chunksize, feed_channels);

    int16_t *audio = calloc(audio_chunksize * feed_channels, sizeof(int16_t));
    assert(audio);

    while (s_running) {
        size_t bytes_read = 0;
        esp_err_t err = i2s_channel_read(
            s_rx_chan,
            audio,
            audio_chunksize * feed_channels * sizeof(int16_t),
            &bytes_read,
            pdMS_TO_TICKS(100));
        if (err == ESP_ERR_TIMEOUT || bytes_read == 0) {
            continue;
        }
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "i2s read failed: %s", esp_err_to_name(err));
            continue;
        }

        int samples = bytes_read / sizeof(int16_t);
        if (feed_channels > 1) {
            for (int i = samples - 1; i >= 0; i--) {
                int16_t mono = audio[i];
                for (int ch = 0; ch < feed_channels; ch++) {
                    audio[i * feed_channels + ch] = mono;
                }
            }
        }
        s_afe->feed(afe_data, audio);
    }

    free(audio);
    vTaskDelete(NULL);
}

static void detect_task(void *arg)
{
    esp_afe_sr_data_t *afe_data = (esp_afe_sr_data_t *)arg;
    const int afe_chunksize = s_afe->get_fetch_chunksize(afe_data);

    char *mn_name = esp_srmodel_filter(s_models, ESP_MN_PREFIX, ESP_MN_ENGLISH);
    if (!mn_name) {
        ESP_LOGE(TAG, "No English MultiNet model found in model partition");
        vTaskDelete(NULL);
        return;
    }
    ESP_LOGI(TAG, "multinet=%s", mn_name);

    esp_mn_iface_t *multinet = esp_mn_handle_from_name(mn_name);
    model_iface_data_t *model_data = multinet->create(mn_name, 6000);
    int mn_chunksize = multinet->get_samp_chunksize(model_data);
    ESP_LOGI(TAG, "afe_chunksize=%d mn_chunksize=%d", afe_chunksize, mn_chunksize);
    assert(mn_chunksize == afe_chunksize);

    register_buddy_commands();
    multinet->print_active_speech_commands(model_data);

    printf("event voice:boot\n");
    printf("event voice:ready\n");

    while (s_running) {
        afe_fetch_result_t *res = s_afe->fetch(afe_data);
        if (!res || res->ret_value == ESP_FAIL) {
            ESP_LOGW(TAG, "afe fetch failed");
            continue;
        }

        if (res->wakeup_state == WAKENET_DETECTED ||
            res->wakeup_state == WAKENET_CHANNEL_VERIFIED) {
            s_awake = true;
            multinet->clean(model_data);
            printf("event voice:wake\n");
        }

        if (!s_awake) {
            continue;
        }

        esp_mn_state_t mn_state = multinet->detect(model_data, res->data);
        if (mn_state == ESP_MN_STATE_DETECTING) {
            continue;
        }

        if (mn_state == ESP_MN_STATE_DETECTED) {
            esp_mn_results_t *mn_result = multinet->get_results(model_data);
            if (mn_result && mn_result->num > 0) {
                int command_id = mn_result->command_id[0];
                const char *event_name = event_for_command(command_id);
                printf("event voice:cmd %s id=%d prob=%.3f\n",
                       event_name,
                       command_id,
                       mn_result->prob[0]);
            }
            continue;
        }

        if (mn_state == ESP_MN_STATE_TIMEOUT) {
            s_awake = false;
            s_afe->enable_wakenet(afe_data);
            printf("event voice:timeout\n");
            continue;
        }
    }

    multinet->destroy(model_data);
    vTaskDelete(NULL);
}

void app_main(void)
{
    ESP_LOGI(TAG, "CYD Buddy XIAO voice target booting");

    ESP_ERROR_CHECK(init_pdm_mic());

    s_models = esp_srmodel_init("model");
    if (!s_models) {
        ESP_LOGE(TAG, "No ESP-SR model partition found. Flash WakeNet/MultiNet models to partition label 'model'.");
        printf("event voice:error no_model_partition\n");
        return;
    }

    afe_config_t *afe_config = afe_config_init("M", s_models, AFE_TYPE_SR, AFE_MODE_LOW_COST);
    s_afe = esp_afe_handle_from_config(afe_config);
    s_afe_data = s_afe->create_from_config(afe_config);
    afe_config_free(afe_config);

    s_afe->print_pipeline(s_afe_data);

    xTaskCreatePinnedToCore(feed_task, "voice_feed", 8 * 1024, s_afe_data, 5, NULL, 0);
    xTaskCreatePinnedToCore(detect_task, "voice_detect", 10 * 1024, s_afe_data, 5, NULL, 1);
}
