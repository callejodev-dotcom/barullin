"""El simulador de Barullín (base/tools/build.py --host) sin Pillow ni FriBiDi: las cabeceras generadas (la fuente,
las palabras) van ya hechas en tools/host_gen. Uso: python3 tools/host_build.py"""
import os
import shutil
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path.insert(0, os.path.join(ROOT, "base", "tools"))
import build  # noqa: E402

_gen = build.generate


def generate():
    try:
        _gen()                                    # con Pillow (y raqm) a mano: las de verdad
    except (SystemExit, Exception):
        build.GEN.mkdir(parents=True, exist_ok=True)
        for f in os.listdir(os.path.join(HERE, "host_gen")):
            shutil.copy(os.path.join(HERE, "host_gen", f), build.GEN / f)


build.generate = generate
sys.argv = ["build.py", os.path.join(ROOT, "app"), "--host"]
sys.exit(build.main())
