#include <stdio.h>
#include "esp_adc/adc_oneshot.h"
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include "esp_adc/adc_cali.h"
#include "driver/gpio.h"
#include "esp_rom_sys.h"
#include "esp_timer.h"

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
