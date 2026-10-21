#include <stdio.h>
#include "mqtt_client.h"
#include "esp_adc/adc_oneshot.h"
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include "esp_adc/adc_cali.h"
#include "driver/gpio.h"
#include "esp_rom_sys.h"
#include "esp_timer.h"
#include "nvs_flash.h"
#include "esp_netif.h"
#include "esp_event.h"
#include "esp_wifi.h"
#include "credenciais_wifi.h"

esp_mqtt_client_handle_t cliente_mqtt;

void wifi_event_handler(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data) {

    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) { // driver Wi-Fi acabou de iniciar
        esp_wifi_connect();
    }

    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {// a conexao caiu ou a tentativa falhou
        esp_wifi_connect();
    }

    if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {// recebeu um endereco IP: esta conectado de verdade
        printf("Conectado na rede !\n");
        esp_mqtt_client_start(cliente_mqtt);
    }
}

void mqtt_event_handler(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data) {
    switch (event_id) {
        case MQTT_EVENT_CONNECTED:
            esp_mqtt_client_publish(cliente_mqtt, "hidrico/principal/status", "online", 0, 1, 1);
            break;
    }
}


void app_main(void){

    int condutividade_final;
    float tensao_volts, temperatura = 25.0, coeficiente_compensacao, tensao_compensada, tds_ppm, condutividade_us_cm;

    adc_oneshot_unit_handle_t adc1_handle;

    adc_oneshot_unit_init_cfg_t init_config1 = {
        .unit_id = ADC_UNIT_1,
    };

    adc_oneshot_new_unit(&init_config1, &adc1_handle);

    adc_oneshot_chan_cfg_t config = {
        .atten = ADC_ATTEN_DB_12,
        .bitwidth = ADC_BITWIDTH_DEFAULT,
    };

    adc_oneshot_config_channel(adc1_handle, ADC_CHANNEL_6, &config);

    adc_cali_handle_t cali_handle;

    adc_cali_line_fitting_config_t cali_config = {
        .unit_id = ADC_UNIT_1,
        .atten = ADC_ATTEN_DB_12,
        .bitwidth = ADC_BITWIDTH_DEFAULT,
        .default_vref = 1100,
    };

    ESP_ERROR_CHECK(adc_cali_create_scheme_line_fitting(&cali_config, &cali_handle));

    gpio_config_t config_trig = {
        .pin_bit_mask = (1ULL << 25),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&config_trig);

    gpio_config_t config_echo = {
        .pin_bit_mask = (1ULL << 26),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&config_echo);

    esp_err_t ret = nvs_flash_init();

    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }

    ESP_ERROR_CHECK(ret);
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    esp_mqtt_client_config_t mqtt_cfg = {
       .broker.address.uri = CONFIG_BROKER_URL,
       .credentials.client_id = "principal",
       .session.keepalive = 15,
       .session.last_will = {.topic = "hidrico/principal/status", .msg = "offline", .qos = 1, .retain = 1,},
    };

   cliente_mqtt = esp_mqtt_client_init(&mqtt_cfg);
   esp_mqtt_client_register_event(cliente_mqtt, ESP_EVENT_ANY_ID, mqtt_event_handler, NULL);

    esp_event_handler_instance_t instancia_wifi, instancia_ip;
    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL, &instancia_wifi));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event_handler, NULL, &instancia_ip));

    wifi_config_t wifi_config = {
        .sta = {
            .ssid = WIFI_SSID,
            .password = WIFI_PASSWORD,
        },
    };

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());

    while(1){

        int mediana_bruta, leitura_bruta, tensao_mv, leituras_tds[10];

        int falha_sensor = 0;
        float duracao, distancia_cm, distancia_m, distancia_final, leituras_hcsr[5];

        for (int i = 0; i < 10; i++) {

            adc_oneshot_read(adc1_handle, ADC_CHANNEL_6, &leitura_bruta);

            leituras_tds[i] = leitura_bruta;
        }

        for(int i = 0; i < 9; i++){
            for(int j = 0; j < 9; j++){
                if(leituras_tds[j] > leituras_tds[j + 1]){
                    int temp = leituras_tds[j];
                    leituras_tds[j] = leituras_tds[j + 1];
                    leituras_tds[j + 1] = temp;
                }
            }
        }

        mediana_bruta = (leituras_tds[4] + leituras_tds[5]) / 2;


        adc_cali_raw_to_voltage(cali_handle, mediana_bruta, &tensao_mv);

        tensao_volts = tensao_mv / 1000.0;
        coeficiente_compensacao = 1.0 + 0.02 * (temperatura - 25.0);
        tensao_compensada = tensao_volts / coeficiente_compensacao;

        tds_ppm = (0.5 * (133.42 * tensao_compensada * tensao_compensada * tensao_compensada - 255.86 * tensao_compensada * tensao_compensada +
            857.39 * tensao_compensada));

        condutividade_us_cm = tds_ppm / 0.5;

        condutividade_final = (int)(condutividade_us_cm + 0.5);
        if(condutividade_final < 0){
            condutividade_final = 0;
        }

        vTaskDelay(1000 / portTICK_PERIOD_MS);

        for (int i = 0; i < 5; i++) {

            gpio_set_level(25, 1);
            esp_rom_delay_us(10);
            gpio_set_level(25, 0);

            int64_t inicio_subida = esp_timer_get_time();
            while (gpio_get_level(26) == 0) {
                if (esp_timer_get_time() - inicio_subida > 30000) {// sensor não respondeu a tempo
                    falha_sensor = 1;
                    break;
                }
            }
            int64_t tempo_subida = esp_timer_get_time();

            int64_t inicio_descida = esp_timer_get_time();
            while (gpio_get_level(26) == 1) {
                if (esp_timer_get_time() - inicio_descida > 30000) {// sensor não respondeu a tempo
                    falha_sensor = 1;
                    break;
                }
            }
            int64_t tempo_descida = esp_timer_get_time();

            duracao = tempo_descida - tempo_subida;
            distancia_cm = duracao / 57.3;
            distancia_m = distancia_cm / 100.0;

            leituras_hcsr[i] = distancia_m;

            vTaskDelay(100 / portTICK_PERIOD_MS);
        }

        if (falha_sensor == 0){
            for(int i = 0; i < 4; i++){
                for(int j = 0; j < 4; j++){
                    if(leituras_hcsr[j] > leituras_hcsr[j + 1]){
                        float temp = leituras_hcsr[j];
                        leituras_hcsr[j] = leituras_hcsr[j + 1];
                        leituras_hcsr[j + 1] = temp;
                    }
                }
            }
            distancia_final = leituras_hcsr[2];

            printf("{\"versao\":1,\"distancia_m\":%.3f,\"condutividade_us_cm\":%d,\"temperatura_c\":%.1f}\n",
                distancia_final, condutividade_final, temperatura);

            vTaskDelay(100 / portTICK_PERIOD_MS);
        }
        else{
            printf("{\"versao\":1,\"distancia_m\":null,\"condutividade_us_cm\":%d,\"temperatura_c\":%.1f}\n",
                condutividade_final, temperatura);

            vTaskDelay(100 / portTICK_PERIOD_MS);
        }
    }
}

