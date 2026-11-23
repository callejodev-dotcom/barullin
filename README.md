# Barullín

Un instrumento de acordes de bolsillo para el **M-VAVE FM-1**, al estilo HiChord: siete teclas tocan los acordes de una tonalidad, las teclas negras los transforman y el resto de las blancas los rasguean. El pequeño de la familia Barullo.

Construido sobre [fm1-base](https://github.com/charlesvestal/fm1-base) de Charles Vestal (MPL-2.0), que pone la plataforma: pantalla, teclas, audio, MIDI, guardado e instalación.

## Cómo se toca

| Control | Qué hace |
| --- | --- |
| Teclas blancas 1–7 | Grados I a vii de la tonalidad y el modo (en Do mayor: C Dm Em F G Am Bdim) |
| Teclas blancas 8–16 | Placa de rasgueo: cada tecla, una nota del acorde, de grave a agudo |
| Teclas negras (mantener) | `m/M` `7` `maj7` · `sus2` `sus4` · `9` `6` `dim/aug` · `INV1` `INV2` |
| Última tecla negra | STOP: pulsación corta para el acorde; mantenida un segundo activa o quita el HOLD (activado por defecto: el acorde sigue sonando al soltar) |
| K1 / K2 / K3 / K4 | Tonalidad / modo / octava / brillo del pad |
| PRESET | Encadenar acordes (voice leading) sí o no |
| OCT− / OCT+ | Octava |
| SAVE | Guarda los ajustes |

Los modificadores se combinan: `7` + `9` da maj9, m9 o 9 según el grado; `sus4` + `7` da 7sus4.

**MIDI de salida** (USB y Bluetooth): rasgueo en el canal 1, acorde en el 2, bajo en el 3.

## Estructura

```
app/            el firmware (lo que se compila para el FM-1)
  app.c         instrumento: entrada, sonido, pantalla
  app.h         contrato con la plataforma
  fm1.toml      ajustes de compilación
  core/         motor de acordes en C puro (sin hardware)
  tests/        guiones del simulador
base/           fm1-base, como submódulo (no se toca)
tests/          pruebas del motor en el ordenador
```

## Compilar y probar

```sh
git clone --recursive https://github.com/callejodev-dotcom/barullin
cd barullin

make test                                      # pruebas del motor de acordes
python3 base/tools/build.py app --host         # el simulador
app/build/host/barullin_host app/tests/basic.txt app/build/host   # capturas y audio en app/build/host
python3 base/tools/build.py app                # el firmware (.fwsc): toolchain de JieLi, ver base/README.md
python3 base/tools/fm1_install.py app/build/barullin.fwsc         # instalar en el FM-1 por USB
```

## Hoja de ruta

- [x] Acordes por grados, modos y modificadores, con voice leading
- [x] Rasgueo, bajo y pad; salida MIDI
- [ ] Arpegiador sincronizado
- [ ] Ritmos de acompañamiento
- [ ] Secuenciador de progresiones
- [ ] Reloj MIDI y start/stop para el directo
- [ ] Motor de pad tipo Juno (de Barullo Xuno)

## Licencia

AGPL-3.0-or-later. fm1-base es MPL-2.0 (ver `base/LICENSING.md`).
