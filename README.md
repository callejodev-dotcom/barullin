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
| ARP (pulsar) | Por pasos: 1, solo arpegio (sin pad ni bajo); 2, arpegio + pad; 3, apagado. Encendido, K1–K4 son del arpegiador |
| FX | Por pasos: 1, DELAY (K1 sí/no, K2 tiempo al tempo de 1/16 a 1/2, K3 repeticiones, K4 mezcla); 2, REVERB (K1 sí/no, K2 tamaño, K3 tono, K4 mezcla); 3, los knobs vuelven |
| LFO | Los knobs pasan al LFO del pad: K1 destino (no, filtro, volumen, afinar, paneo), K2 velocidad (0,1–10 Hz), K3 cantidad, K4 forma (seno, triángulo, cuadrada, azar). Apagado de serie |
| SEL | Bajo pulsado: los knobs pasan a K1 sí/no, K2 paso (1/4, 1/8, 1/8T, 1/16), K3 patrón (nota, octavas, quinta), K4 duración. Independiente del arpegiador; MIDI canal 3. SEL otra vez los devuelve |
| ENV | Por pasos: 1, envolvente del pad; 2, del arpegio; 3, del bajo (el de siempre y el arpegiado); 4, fuera. K1 ataque (2 ms–2 s), K2 caída (50 ms–3 s), K3 sostenido (0–100 %), K4 release (20 ms–4 s) |
| HOME | Los knobs vuelven al acorde (el arpegio y los efectos siguen). Después, ARP o FX vuelven a la página donde estabas, sin cambiar nada |
| ARP mantenido + K1 / K2 / K3 / K4 | Modo (sube, baja, sube-baja, azar) / paso (1/8, 1/8T, 1/16) / octavas (1–3) / duración de cada nota |
| SELECT | Tempo (40–220 BPM) |
| REC | Graba una idea: empieza con el primer acorde que toques; REC otra vez la cierra (a compases enteros) y la pone en bucle |
| PLAY | Reproduce o para la idea del banco. Mientras suena, lo que toques en directo manda |
| SEQ | Banco siguiente (8 bancos, cada uno con su idea). Si algo suena, el cambio entra al acabar el bucle |
| OCT− / OCT+ | Octava |
| SAVE | Guarda los ajustes y los 8 bancos |

Los modificadores se combinan: `7` + `9` da maj9, m9 o 9 según el grado; `sus4` + `7` da 7sus4.

**MIDI de salida** (USB y Bluetooth): rasgueo en el canal 1, acorde en el 2, bajo en el 3, arpegio en el 4; con el arpegiador encendido, reloj MIDI con start/stop.

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
- [x] Arpegiador con reloj MIDI
- [ ] Ritmos de acompañamiento
- [x] Grabar y repetir ideas (REC / PLAY)
- [ ] Reloj MIDI y start/stop para el directo
- [x] Delay de rebote al tempo y reverb
- [ ] Motor de pad tipo Juno (de Barullo Xuno)

## Licencia

AGPL-3.0-or-later. fm1-base es MPL-2.0 (ver `base/LICENSING.md`).
