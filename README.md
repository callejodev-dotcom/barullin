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
| K1 / K2 / K3 / K4 | En la página principal (HOME): volumen del pad / del bajo / del arpegio / del rasgueo, en pasos del 5 % (una vuelta, de 0 a 100 %). Todo suena a la vez: lo que no quieras, a 0. El arpegio viene a 0 y suena en cuanto le subes el volumen |
| PRESET | Encadenar acordes (voice leading) sí o no |

### Páginas

Los knobs K1–K4 son de la página en la que estás. Cada botón recorre sus páginas (de la última vuelve a la primera). Si vienes de otra parte, te lleva a la página que tiene algo encendido (si hay varias, a la última que visitaste) y, si no hay ninguna, a la última suya en la que estuviste. **SEL enciende o apaga** lo de la página en la que estás, y se ilumina cuando está encendido. La luz de ARP, FX y LFO sigue encendida mientras algo de su sección suena. **HOME** vuelve a la página principal; lo que suena sigue sonando.

| Botón | Página | SEL | K1 | K2 | K3 | K4 |
| --- | --- | --- | --- | --- | --- | --- |
| ARP | Arpegiador | silencio rápido (a 0 y vuelta) | octavas (1–3) | modo (sube, baja, sube-baja, azar) | paso (1/8, 1/8T, 1/16) | duración |
| | Bajo pulsado | sí / no | paso (1/4, 1/8, 1/8T, 1/16) | patrón (nota, octavas, quinta) | duración | octava (−1, 0, +1) |
| FX | Delay | sí / no | tiempo al tempo (1/16–1/2) | repeticiones | mezcla | tono de las repeticiones |
| | Reverb | sí / no | sala | tono | mezcla | ancho estéreo |
| ENV | Pad, arpegio, bajo | — | ataque (2 ms–2 s) | caída (50 ms–3 s) | sostenido (0–100 %) | release (20 ms–4 s) |
| LFO | Pad, arpegio, bajo | sí / no | destino (filtro/brillo, volumen, afinar, paneo) | velocidad (0,1–10 Hz) | cantidad | forma (seno, triángulo, cuadrada, azar) |
| GLO | Global | — | tonalidad | modo | octava | brillo del pad |
| HOME | Principal | — | volumen pad | volumen bajo | volumen arpegio | volumen rasgueo |

Pad, bajo, arpegio y rasgueo son capas independientes que suenan a la vez; cada una tiene su volumen en HOME. El bajo pulsado sustituye al sostenido mientras está encendido. El arpegiador y el bajo pulsado van a la misma rejilla del tempo. Efectos y LFO vienen apagados de serie.

### Más controles

| Control | Qué hace |
| --- | --- |
| SELECT | Tempo (40–220 BPM) |
| REC | Graba una idea: empieza con el primer acorde que toques; REC otra vez la cierra (a compases enteros) y la pone en bucle |
| PLAY | Reproduce o para la idea del banco. Mientras suena, lo que toques en directo manda |
| SEQ | Banco siguiente (8 bancos, cada uno con su idea). Si algo suena, el cambio entra al acabar el bucle |
| OCT− / OCT+ | Octava |
| SAVE | Guarda los ajustes y los 8 bancos |

Los modificadores se combinan: `7` + `9` da maj9, m9 o 9 según el grado; `sus4` + `7` da 7sus4.

**MIDI de salida** (USB y Bluetooth): rasgueo en el canal 1, acorde en el 2, bajo en el 3 (también el pulsado), arpegio en el 4; con el arpegiador o el bajo pulsado encendidos, reloj MIDI con start/stop.

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
- [x] Reloj MIDI y start/stop
- [x] Bajo pulsado, envolventes y LFO por sonido
- [x] Delay de rebote al tempo y reverb
- [ ] Motor de pad tipo Juno (de Barullo Xuno)

## Licencia

AGPL-3.0-or-later. fm1-base es MPL-2.0 (ver `base/LICENSING.md`).
