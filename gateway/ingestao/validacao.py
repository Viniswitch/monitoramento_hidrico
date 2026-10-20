NOS_VALIDOS = {"reserva", "principal", "saida"}
TIPOS_VALIDOS = {"telemetria", "status"}

def interpretar_topico(topico):
    partes = topico.split("/")
    if len(partes) != 3:
        raise ValueError(f"topico deve ter 3 partes, mas tem {len(partes)}: {topico}")
    
    prefixo, no, tipo = partes
    if prefixo != "hidrico":
        raise ValueError(f"prefixo deve ser 'hidrico', mas e '{prefixo}': {topico}")
    if no not in NOS_VALIDOS:
        raise ValueError(f"no desconhecido '{no}': {topico}")
    if tipo not in TIPOS_VALIDOS:
        raise ValueError(f"tipo desconhecido '{tipo}': {topico}")
    return no, tipo


if __name__ == "__main__":
    for t in [
        "hidrico/principal/telemetria",
        "hidrico/reserva/status",
        "hidrico/cozinha/telemetria",
        "hidrico/principal/temperatura",
        "outro/principal/telemetria",
        "hidrico/principal",
        "hidrico/principal/telemetria/extra",
    ]:
        try:
            resultado = interpretar_topico(t)
            print(t, "->", resultado)
        except ValueError as erro:
            print("REJEITADO:", erro)
            