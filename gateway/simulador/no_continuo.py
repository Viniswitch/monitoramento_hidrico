import json
import time
import random
import sys
import threading

import paho.mqtt.client as mqtt

BROKER_HOST = "localhost"
BROKER_PORT = 1883
INTERVALO_S = 5

# Parametros HIPOTETICOS do reservatorio Principal (contrato, secao 6):
#   H = (sensor ate o fundo) / H_UTIL = (nivel da agua ate o fundo com o reservatorio cheio) / d = (distância do sensor até o nível da água)
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

class NoSimulado:
    def __init__(self, nome, base, mede_qualidade=False):
        self.nome = nome
        self.base = base
        self.mede_qualidade = mede_qualidade
        self.topico_telemetria = f"hidrico/{nome}/telemetria"
        self.topico_status = f"hidrico/{nome}/status"
        self.conectado = threading.Event()
        self.client = mqtt.Client(mqtt.CallbackAPIVersion.VERSION2, client_id=nome)
        self.client.on_connect = self._ao_conectar
        self.client.will_set(self.topico_status, "offline", qos=1, retain=True)

    def _ao_conectar(self, client, userdata, flags, reason_code, properties):
        if reason_code.is_failure:
            print(f"[{self.nome}] Falha ao conectar:", reason_code)
        else:
            print(f"[{self.nome}] Conectado ao broker")
            client.publish(self.topico_status, "online", qos=1, retain=True)
            self.conectado.set()

    def iniciar(self):
        self.client.connect(BROKER_HOST, BROKER_PORT, keepalive=10)
        self.client.loop_start()
        if not self.conectado.wait(timeout=10):
            print(f"[{self.nome}] Nao foi possivel conectar ao broker em 10 s")
            self.client.loop_stop()
            sys.exit(1)

    def gerar_leitura(self):
        distancia = self.base["distancia"] + random.uniform(-0.003, 0.003)
        leitura = {"versao": 1, "distancia_m": round(distancia, 3)}
        if self.mede_qualidade:
            temperatura = self.base["temperatura"] + random.uniform(-0.2, 0.2)
            if self.base["condutividade"] is None:
                condutividade = None
            else:
                condutividade = max(0, round(self.base["condutividade"] + random.uniform(-10, 10)))
            leitura["condutividade_us_cm"] = condutividade
            leitura["temperatura_c"] = round(temperatura, 1)
        return leitura

    def publicar_telemetria(self):
        dados = self.gerar_leitura()
        self.client.publish(self.topico_telemetria, json.dumps(dados), qos=0)
        print(f"[{self.nome}] Telemetria enviada")

    def encerrar(self):
        info = self.client.publish(self.topico_status, "offline", qos=1, retain=True)
        info.wait_for_publish()
        self.client.disconnect()
        self.client.loop_stop()

cenario = sys.argv[1] if len(sys.argv) > 1 else "normal"
if cenario not in CENARIOS:
    print("Cenario invalido:", cenario)
    print("Opcoes:", ", ".join(CENARIOS))
    sys.exit(1)

nos = [
    NoSimulado("reserva", {"distancia": 1.204}),
    NoSimulado("principal", CENARIOS[cenario], mede_qualidade=True),
    NoSimulado("saida", {"distancia": 0.800}),
]

for no in nos:
    no.iniciar()

try:
    while True:
        for no in nos:
            no.publicar_telemetria()
        time.sleep(INTERVALO_S)
except KeyboardInterrupt:
    print("Encerrando (desligamento planejado)")
    for no in nos:
        no.encerrar()