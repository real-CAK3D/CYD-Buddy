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
    {1, "take_picture", "take picture"},
    {2, "take_picture", "snap picture"},
    {3, "take_picture", "take photo"},
    {4, "look_around", "look around"},
    {5, "scan_room", "scan room"},
    {6, "what_do_you_see", "describe view"},
    {7, "what_do_you_see", "describe room"},
    {8, "tell_joke", "tell joke"},
    {9, "tell_joke", "joke mode"},
    {10, "say_something_rude", "be rude"},
    {11, "be_nice", "be nice"},
    {12, "be_sarcastic", "be sarcastic"},
    {13, "be_quiet", "be quiet"},
    {14, "talk_more", "talk more"},
    {15, "wake_up", "wake up"},
    {16, "go_to_sleep", "sleep"},
    {17, "happy_mode", "fun mode"},
    {18, "sad_mode", "sad mode"},
    {19, "angry_mode", "get mad"},
    {20, "suspicious_mode", "suspicious mode"},
    {21, "love_mode", "love mode"},
    {22, "remember_me", "remember me"},
    {23, "forget_me", "forget me"},
    {24, "connect_wifi", "connect wifi"},
    {25, "status", "status"},
    {26, "weather", "weather"},
    {27, "time", "time"},
    {28, "date", "date"},
    {29, "battery", "power level"},
    {30, "system_status", "system check"},
    {31, "mood_status", "mood status"},
    {32, "who_are_you", "buddy identity"},
    {33, "what_is_name", "buddy name"},
    {34, "my_name", "owner name"},
    {35, "greet", "greet me"},
    {36, "compliment", "be sweet"},
    {37, "insult", "roast me"},
    {38, "calm_down", "calm down"},
    {39, "focus", "focus"},
    {40, "listen", "listen"},
    {41, "stop_listening", "stop listening"},
    {42, "start_ai", "start brain"},
    {43, "stop_ai", "stop brain"},
    {44, "use_offline", "offline mode"},
    {45, "use_online", "online mode"},
    {46, "use_ollama", "use ollama"},
    {47, "take_note", "take note"},
    {48, "read_note", "read note"},
    {49, "save_memory", "store memory"},
    {50, "clear_memory", "erase memory"},
    {51, "learn_this", "learn this"},
    {52, "forget_that", "forget that"},
    {53, "camera_on", "camera on"},
    {54, "camera_off", "camera off"},
    {55, "mic_on", "mic on"},
    {56, "mic_off", "mic off"},
    {57, "louder", "talk louder"},
    {58, "quieter", "talk softer"},
    {59, "repeat", "repeat that"},
    {60, "explain", "explain it"},
    {61, "yes", "answer yes"},
    {62, "no", "no"},
    {63, "maybe", "answer maybe"},
    {64, "thank_you", "thanks pal"},
    {65, "good_buddy", "nice buddy"},
    {66, "bad_buddy", "mean buddy"},
    {67, "bored", "bored"},
    {68, "entertain_me", "entertain me"},
    {69, "dance", "dance"},
    {70, "sing", "sing"},
    {71, "laugh", "laugh"},
    {72, "get_excited", "excited mode"},
    {73, "relax", "stay calm"},
    {74, "night_mode", "night mode"},
    {75, "day_mode", "day eyes"},
    {76, "bright_eyes", "eye glow"},
    {77, "dark_eyes", "eye dim"},
    {78, "random_mood", "random mood"},
    {79, "manual_mode", "manual mode"},
    {80, "auto_mode", "auto mode"},
    {81, "help", "help"},
    {82, "menu", "menu"},
    {83, "settings", "settings"},
    {84, "pair_cyd", "pair display"},
    {85, "xiao_status", "sense status"},
    {86, "take_snapshot", "snapshot"},
    {87, "start_snapshots", "start snapshots"},
    {88, "stop_snapshots", "stop snapshots"},
    {89, "see_me", "see me"},
    {90, "remember_face", "remember face"},
    {91, "forget_face", "forget face"},
    {92, "identify_me", "recognize me"},
    {93, "hear_sound", "hear sound"},
    {94, "noise_status", "noise status"},
    {95, "wake_name", "wake name"},
    {96, "train_name", "train name"},
    {97, "call_me", "call me"},
    {98, "say_name", "speak name"},
    {99, "chill", "chill"},
    {100, "attitude", "attitude"},
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

static bool register_buddy_commands(void)
{
    esp_err_t err = esp_mn_commands_clear();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "voice command clear failed: %s", esp_err_to_name(err));
        printf("event voice:error command_clear_failed\n");
        return false;
    }

    int accepted = 0;
    for (size_t i = 0; i < sizeof(COMMAND_EVENTS) / sizeof(COMMAND_EVENTS[0]); i++) {
        ESP_LOGI(TAG, "voice cmd %d: %s -> %s",
                 COMMAND_EVENTS[i].command_id,
                 COMMAND_EVENTS[i].phrase,
                 COMMAND_EVENTS[i].event_name);
        err = esp_mn_commands_add(
            COMMAND_EVENTS[i].command_id,
            (char *)COMMAND_EVENTS[i].phrase);
        if (err == ESP_OK) {
            accepted++;
        } else {
            ESP_LOGW(TAG, "voice cmd rejected: id=%d phrase=\"%s\" err=%s",
                     COMMAND_EVENTS[i].command_id,
                     COMMAND_EVENTS[i].phrase,
                     esp_err_to_name(err));
        }
    }

    if (accepted == 0) {
        ESP_LOGE(TAG, "No valid voice commands were accepted by MultiNet");
        printf("event voice:error no_valid_commands\n");
        return false;
    }

    esp_mn_error_t *command_errors = esp_mn_commands_update();
    if (command_errors) {
        ESP_LOGW(TAG, "Some voice commands were rejected by MultiNet");
    }
    esp_mn_commands_print();
    esp_mn_active_commands_print();
    ESP_LOGI(TAG, "voice commands accepted: %d/%d",
             accepted,
             (int)(sizeof(COMMAND_EVENTS) / sizeof(COMMAND_EVENTS[0])));
    return true;
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

    if (!register_buddy_commands()) {
        vTaskDelete(NULL);
        return;
    }
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
