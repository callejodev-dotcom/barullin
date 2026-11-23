#!/bin/sh
# Doble clic: saca las ideas del FM-1 (conectado por USB, con Barullín, parado) a MIDI y a proyectos de Ableton, y
# abre la carpeta. La primera vez prepara lo que hace falta.
cd "$(dirname "$0")" || exit 1
if [ ! -d .venv ]; then
    echo "preparando (solo la primera vez)..."
    python3 -m venv .venv && . .venv/bin/activate && pip install -q mido python-rtmidi || { echo "no pude preparar Python"; read x; exit 1; }
else
    . .venv/bin/activate
fi
git pull -q 2>/dev/null
python3 tools/barullin_export.py && open "Barullin Export"
echo
echo "listo. (puedes cerrar esta ventana)"
