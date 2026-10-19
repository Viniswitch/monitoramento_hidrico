import json
import time
import paho.mqtt.client as mqtt
import random
import sys
import threading

BROKER_HOST = "localhost"
BROKER_PORT = 1883
NO = "principal"
TOPICO_TELEMETRIA = f"hidrico/{NO}/telemetria"
TOPICO_STATUS = f"hidrico/{NO}/status"
INTERVALO_S = 5
conectado = threading.Event()

# Parametros HIPOTETICOS do reservatorio Principal (contrato, secao 6):
#   H = (sensor ate o fundo) / H_UTIL = (nivel da agua ate o fundo) / d = (distância do sensor até o nível da água)
#   H_SONDA = (Altura em que a sonda de condutividade está instalada, medida a partir do fundo)
#   H = 1.50 m , H_UTIL = 1.20 m , H_SONDA = 0.05 m
#   d = H - (nivel% / 100) * H_UTIL
CENARIOS = {
    "normal":             {"distancia": 0.512, "condutividade": 780,  "temperatura": 27.1},
    "nivel_baixo":        {"distancia": 1.404, "condutividade": 780,  "temperatura": 27.1},  # 8 %
    "condutividade_alta": {"distancia": 0.512, "condutividade": 1200, "temperatura": 27.1},
    "sonda_seca":         {"distancia": 1.470, "condutividade": 0,    "temperatura": 27.1},  # h = 0,03 m
    "sensor_falho":       {"distancia": 0.512, "condutividade": None, "temperatura": 27.1},
}

cenario = sys.argv[1] if len(sys.argv) > 1 else "normal"
if cenario not in CENARIOS:
    print("Cenario invalido:", cenario)
    print("Opcoes:", ", ".join(CENARIOS))
    sys.exit(1)


def ao_conectar(client, userdata, flags, reason_code, properties):
    if reason_code.is_failure:
        print("Falha ao conectar:", reason_code)
    else:
        print("Conectado ao broker")
        client.publish(TOPICO_STATUS, "online", qos=1, retain=True)
        conectado.set()


def gerar_leitura():
    base = CENARIOS[cenario]
    distancia = base["distancia"] + random.uniform(-0.003, 0.003)
    temperatura = base["temperatura"] + random.uniform(-0.2, 0.2)
    if base["condutividade"] is None:
        condutividade = None
    else:
        condutividade = max(0, round(base["condutividade"] + random.uniform(-10, 10)))
    return {
        "versao": 1,
        "distancia_m": round(distancia, 3),
        "condutividade_us_cm": condutividade,
        "temperatura_c": round(temperatura, 1),
    }


client = mqtt.Client(mqtt.CallbackAPIVersion.VERSION2, client_id=NO)
client.on_connect = ao_conectar

client.will_set(TOPICO_STATUS, "offline", qos=1, retain=True)
client.connect(BROKER_HOST, BROKER_PORT, keepalive=10)

client.loop_start()

if not conectado.wait(timeout=10):
    print("Nao foi possivel conectar ao broker em 10 s")
    client.loop_stop()
    sys.exit(1)


try:
    while True:
        dados = gerar_leitura()
        client.publish(TOPICO_TELEMETRIA, json.dumps(dados), qos=0)
        print("Telemetria enviada")
        time.sleep(INTERVALO_S)


except KeyboardInterrupt:
    print("Encerrando (desligamento planejado)")
    info = client.publish(TOPICO_STATUS, "offline", qos=1, retain=True)
    info.wait_for_publish()
    client.disconnect()

    client.loop_stop()