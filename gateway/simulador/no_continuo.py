import json
import time
import paho.mqtt.client as mqtt

BROKER_HOST = "localhost"
BROKER_PORT = 1883
NO = "principal"
TOPICO_TELEMETRIA = f"hidrico/{NO}/telemetria"
TOPICO_STATUS = f"hidrico/{NO}/status"
INTERVALO_S = 5


def ao_conectar(client, userdata, flags, reason_code, properties):
    if reason_code.is_failure:
        print("Falha ao conectar:", reason_code)
    else:
        print("Conectado ao broker")
        client.publish(TOPICO_STATUS, "online", qos=1, retain=True)


client = mqtt.Client(mqtt.CallbackAPIVersion.VERSION2, client_id=NO)
client.on_connect = ao_conectar

client.will_set(TOPICO_STATUS, "offline", qos=1, retain=True)
client.connect(BROKER_HOST, BROKER_PORT, keepalive=10)

client.loop_start()


try:
    while True:
        dados = {"versao": 1, "distancia_m": 0.512, "condutividade_us_cm": 780, "temperatura_c": 27.1,}
        client.publish(TOPICO_TELEMETRIA, json.dumps(dados), qos=0)
        print("Telemetria enviada")
        time.sleep(INTERVALO_S)


except KeyboardInterrupt:
    print("Encerrando (desligamento planejado)")
    info = client.publish(TOPICO_STATUS, "offline", qos=1, retain=True)
    info.wait_for_publish()
    client.disconnect()

    client.loop_stop()