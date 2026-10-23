"""API de armazenamento de leituras do robô explorador (Etapa 03)."""

from contextlib import closing
from datetime import datetime, timezone
import math
import os
from pathlib import Path
import sqlite3

from flask import Flask, jsonify, request


BASE_DIR = Path(__file__).resolve().parent
SCHEMA_PATH = BASE_DIR / "schema.sql"
DATABASE_PATH = Path(
    os.environ.get("LEITURAS_DB", str(BASE_DIR / "leituras.db"))
).expanduser()

app = Flask(__name__)


def connect_db():
    DATABASE_PATH.parent.mkdir(parents=True, exist_ok=True)
    connection = sqlite3.connect(DATABASE_PATH, timeout=5)
    connection.row_factory = sqlite3.Row
    return connection


def init_db():
    schema = SCHEMA_PATH.read_text(encoding="utf-8")
    with closing(connect_db()) as connection:
        connection.executescript(schema)
        connection.commit()


def utc_timestamp(value):
    """Normalize an ISO 8601 timestamp to UTC; use server time if omitted."""
    if value is None or value == "":
        parsed = datetime.now(timezone.utc)
    elif not isinstance(value, str):
        raise ValueError("timestamp deve ser texto ISO 8601")
    else:
        try:
            parsed = datetime.fromisoformat(value.strip().replace("Z", "+00:00"))
        except ValueError as exc:
            raise ValueError("timestamp deve estar no formato ISO 8601") from exc
        if parsed.tzinfo is None:
            parsed = parsed.replace(tzinfo=timezone.utc)
        parsed = parsed.astimezone(timezone.utc)

    return parsed.isoformat(timespec="seconds").replace("+00:00", "Z")


def numeric_value(payload, field):
    value = payload.get(field)
    if isinstance(value, bool) or not isinstance(value, (int, float)):
        raise ValueError("{} deve ser numérico".format(field))
    value = float(value)
    if not math.isfinite(value):
        raise ValueError("{} deve ser um número finito".format(field))
    return value


def validate_payload(payload):
    if not isinstance(payload, dict):
        raise ValueError("o corpo deve ser um objeto JSON")

    temperatura = numeric_value(payload, "temperatura_c")
    umidade = numeric_value(payload, "umidade_pct")
    luminosidade = numeric_value(payload, "luminosidade")
    probabilidade = numeric_value(payload, "probabilidade_vida")
    presenca = payload.get("presenca")

    if not -100 <= temperatura <= 150:
        raise ValueError("temperatura_c deve estar entre -100 e 150 °C")
    if not 0 <= umidade <= 100:
        raise ValueError("umidade_pct deve estar entre 0 e 100")
    if luminosidade < 0 or not luminosidade.is_integer():
        raise ValueError("luminosidade deve ser um inteiro não negativo")
    if isinstance(presenca, bool):
        presenca = int(presenca)
    if not isinstance(presenca, int) or presenca not in (0, 1):
        raise ValueError("presenca deve ser 0 ou 1")
    if not 0 <= probabilidade <= 100:
        raise ValueError("probabilidade_vida deve estar entre 0 e 100")

    return {
        "timestamp": utc_timestamp(payload.get("timestamp")),
        "temperatura_c": temperatura,
        "umidade_pct": umidade,
        "luminosidade": int(luminosidade),
        "presenca": presenca,
        "probabilidade_vida": probabilidade,
    }


@app.get("/health")
def health():
    return jsonify({"status": "ok"})


@app.route("/leituras", methods=["GET", "POST"])
def leituras():
    if request.method == "POST":
        payload = request.get_json(silent=True)
        if payload is None:
            return jsonify({"erro": "envie um objeto JSON com Content-Type application/json"}), 400

        try:
            leitura = validate_payload(payload)
        except ValueError as exc:
            return jsonify({"erro": str(exc)}), 400

        with closing(connect_db()) as connection:
            cursor = connection.execute(
                """
                INSERT INTO leituras (
                    timestamp, temperatura_c, umidade_pct, luminosidade,
                    presenca, probabilidade_vida
                ) VALUES (?, ?, ?, ?, ?, ?)
                """,
                (
                    leitura["timestamp"],
                    leitura["temperatura_c"],
                    leitura["umidade_pct"],
                    leitura["luminosidade"],
                    leitura["presenca"],
                    leitura["probabilidade_vida"],
                ),
            )
            connection.commit()
            leitura_id = cursor.lastrowid

        return jsonify({"id": leitura_id, "timestamp": leitura["timestamp"]}), 201

    try:
        limit = int(request.args.get("limit", "100"))
    except ValueError:
        return jsonify({"erro": "limit deve ser um inteiro entre 1 e 100"}), 400
    if not 1 <= limit <= 100:
        return jsonify({"erro": "limit deve estar entre 1 e 100"}), 400

    with closing(connect_db()) as connection:
        rows = connection.execute(
            """
            SELECT id, timestamp, temperatura_c, umidade_pct, luminosidade,
                   presenca, probabilidade_vida
            FROM leituras
            ORDER BY id DESC
            LIMIT ?
            """,
            (limit,),
        ).fetchall()

    # Devolve da leitura mais antiga para a mais recente dentro do lote.
    resultado = [dict(row) for row in reversed(rows)]
    return jsonify({"quantidade": len(resultado), "leituras": resultado})


init_db()


if __name__ == "__main__":
    app.run(host="0.0.0.0", port=5000, debug=False)
