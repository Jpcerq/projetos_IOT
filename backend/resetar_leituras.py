"""Apaga leituras para novos testes, preservando a tabela e a sequência de IDs."""

import argparse
import os
from pathlib import Path
import sqlite3


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--confirmar", action="store_true", help="Confirma a exclusão das leituras."
    )
    args = parser.parse_args()
    if not args.confirmar:
        parser.error("Para apagar os registros, execute com --confirmar.")

    caminho = Path(
        os.environ.get("LEITURAS_DB", str(Path(__file__).parent / "leituras.db"))
    ).expanduser().resolve()
    if not caminho.is_file():
        parser.exit(1, "Banco não encontrado: {}\n".format(caminho))

    try:
        conexao = sqlite3.connect(caminho.as_uri() + "?mode=rw", uri=True, timeout=5)
        try:
            with conexao:
                cursor = conexao.execute("DELETE FROM leituras")
                quantidade = cursor.rowcount
        finally:
            conexao.close()
    except sqlite3.Error as exc:
        parser.exit(1, "Falha ao apagar leituras: {}\n".format(exc))

    print("Banco: {}".format(caminho))
    print("Leituras apagadas: {}".format(quantidade))


if __name__ == "__main__":
    main()
