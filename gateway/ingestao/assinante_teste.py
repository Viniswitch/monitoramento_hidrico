import paho.mqtt.client as mqtt

BROKER_HOST = "localhost"
BROKER_PORT = 1883
TOPICOS = [("hidrico/+/telemetria", 0), ("hidrico/+/status", 1)]


def ao_conectar(client, userdata, flags, reason_code, properties):
    if reason_code.is_failure:
        print("Falha ao conectar:", reason_code)
    else:
        print("Conectado ao broker; assinando topicos")
        client.subscribe(TOPICOS)


def ao_receber(client, userdata, msg):
    texto = msg.payload.decode("utf-8")
    origem = "RETIDA" if msg.retain else "ao vivo"
    print(f"[{origem}] {msg.topic} -> {texto}")


client = mqtt.Client(mqtt.CallbackAPIVersion.VERSION2, client_id="gateway")
client.on_connect = ao_conectar
client.on_message = ao_receber
client.connect(BROKER_HOST, BROKER_PORT, keepalive=10)

try:
    client.loop_forever()
except KeyboardInterrupt:
    print("Encerrando")
    client.disconnect()
    