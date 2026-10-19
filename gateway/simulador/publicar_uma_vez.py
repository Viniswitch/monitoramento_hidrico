import paho.mqtt.client as mqtt
import json

BROKER_HOST = "localhost"
BROKER_PORT = 1883
TOPICO = "hidrico/principal/telemetria"

client = mqtt.Client(mqtt.CallbackAPIVersion.VERSION2, client_id="principal")

client.connect(BROKER_HOST, BROKER_PORT, keepalive=10)

client.loop_start()

dados = {"versao": 1, "distancia_m": 0.512, "condutividade_us_cm": 780, "temperatura_c": 27.1,}
payload = json.dumps(dados)

info = client.publish(TOPICO, payload, qos=0)
info.wait_for_publish()
print("Publicado em", TOPICO, ":", payload)

client.disconnect()

client.loop_stop()
