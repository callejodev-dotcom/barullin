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
| K1 / K2 / K3 / K4 | En la página principal (HOME): volumen del pad / del bajo / del arpegio / del rasgueo, en pasos del 5 % (una vuelta, de 0 a 100 %). Todo suena a la vez: lo que no quieras, a 0. Pad, bajo y arpegio vienen a 0 (de serie solo suena el rasgueo): cada uno suena en cuanto le subes el volumen |
| ALGORITHM (en HOME) | CUADRAR: con algo sonando, el acorde nuevo espera a la siguiente corchea, negra (de serie) o compás, y entra justo en la línea; así el ritmo y el arpegio nunca se cortan. Si lo pulsas un pelín tarde (hasta 60 ms), entra en el acto. AL MOMENTO: cambia nada más pulsar |
| PRESET | Tipo de compás: 4/4, 3/4, 6/8, 5/4, 7/8. Agrupa el arpegio (4/4: 4+4; 3/4 y 6/8: 3+3; 5/4: 4+3+3; 7/8: 4+3), cierra las ideas a compases enteros y cuenta PLAY en él |

### Páginas

Los knobs K1–K4 son de la página en la que estás. Cada botón recorre sus páginas (de la última vuelve a la primera). Si vienes de otra parte, te lleva a la página que tiene algo encendido (si hay varias, a la última que visitaste) y, si no hay ninguna, a la última suya en la que estuviste. **SEL enciende o apaga** lo de la página en la que estás, y se ilumina cuando está encendido. La luz de ARP, FX y LFO sigue encendida mientras algo de su sección suena. **HOME** vuelve a la página principal; lo que suena sigue sonando.

| Botón | Página | SEL | K1 | K2 | K3 | K4 |
| --- | --- | --- | --- | --- | --- | --- |
| ARP | Arpegiador | silencio rápido (a 0 y vuelta) | octavas (1–3) | modo (sube, baja, sube-baja, azar) | paso (1/8, 1/8T, 1/16) | duración (ALGORITHM: ciclo en el compás, grupos de 4, o libre) |
| | Bajo pulsado | sí / no (si el bajo está a 0, lo sube al 60 %) | paso (1/4, 1/8, 1/8T, 1/16) | patrón (nota, quinta, octava, acorde: las notas del que suena) | duración | octava (−1, 0, +1) |
| FX | Delay (a tempo, rebote estéreo) | silencio rápido | tiempo (1/16–1/2) | repeticiones | tono de las repeticiones | mezcla de la capa elegida |
| | Delay libre (en ms) | silencio rápido | tiempo (10–750 ms) | repeticiones | tono | mezcla de la capa elegida |
| | Reverb | silencio rápido | sala | tono | ancho estéreo | mezcla de la capa elegida |
| ENV | Envolvente (ALGORITHM: de pad, arpegio o bajo) | — | ataque (2 ms–2 s) | caída (50 ms–3 s) | sostenido (0–100 %) | release (20 ms–4 s) |
| LFO | LFO 1, 2, 3 (ALGORITHM: destino, ver abajo) | sí / no | velocidad (0,1–10 Hz) | cantidad | forma (seno, triángulo, cuadrada, azar) | a tempo (libre, 1/16 a 4 compases) |
| EDIT | Ritmo | silencio rápido (con todo a 0, pone un ritmo básico) | bombo (a negras) | caja (en el 2 y el 4) | charles (a corcheas) | apertura del charles (de cerrado a abierto) |
| GLO | Global | — | tonalidad | modo | sonido del pad (ver abajo; carga su envolvente, que luego tocas en ENV) | brillo (ALGORITHM: encadenar acordes sí/no) |
| | Bluetooth (cascos y altavoces) | buscar / conectar lo elegido / colgar | — | — | — | salida: el aparato o el FM-1 (ALGORITHM: BUSCAR o un aparato) |
| HOME | Principal | — | volumen pad | volumen bajo | volumen arpegio | volumen rasgueo |

Pad, bajo, arpegio y rasgueo son capas independientes que suenan a la vez; cada una tiene su volumen en HOME. El bajo pulsado sustituye al sostenido mientras está encendido. El arpegiador y el bajo pulsado van a la misma rejilla del tempo. Van siempre con la rejilla del compás, que arranca con el primer acorde: los cambios de acorde caen en ella (CUADRAR), y si uno llega hasta 60 ms tarde, ese paso se repite con él. Con CUADRAR en AL MOMENTO, cada acorde nuevo arranca el arpegio y el bajo en el acto. Efectos y LFO vienen apagados de serie.

Cada efecto lleva su mezcla por capas: en sus páginas, ALGORITHM elige la capa (pad, bajo, arpegio o rasgueo) y K4 cuánto manda esa capa al efecto. Arriba se ven las cuatro (la que lleva K4, resaltada). De serie todas están a 0: un efecto suena en cuanto le subes la mezcla a una capa, como el arpegio con su volumen. SEL es un silencio rápido (y, con todo a 0, pone la capa elegida al 30 %).

**Cada banco, su sonido.** Cada idea (SEQ) guarda su sonido: tono, modo, tempo, compás, sonido del pad, volúmenes, arpegiador, bajo pulsado, envolventes, efectos, LFO y ritmo. Al cambiar de banco suena el suyo; un banco que nunca se tocó empieza con todo de serie. Sonando, el cambio entra con el banco, al acabar el bucle. SAVE lo guarda todo. (Lo global, HOLD, CUADRAR y ENCADENAR, no cambia con el banco.)

**Bluetooth** (GLO, segunda página): el sonido a cascos o altavoces. SEL con BUSCAR busca; ALGORITHM elige el aparato; SEL conecta (y, conectado, cuelga). Recuerda cuatro, y al encenderse vuelve a conectar el último. Mientras suena por Bluetooth, el FM-1 calla (K4 lo devuelve al FM-1 sin colgar). Llega 150–300 ms tarde: para escuchar lo hecho, no para tocar a tiempo.

**Cada sección, su color**: la etiqueta con el nombre de la página, la raya y los nombres de los knobs van en el color de su botón (ARP azul, FX violeta, ENV verde, LFO rosa, EDIT rojo, GLO blanco; HOME, ámbar), y el mensaje de arriba también.

**ALGORITHM, en todo el firmware**, elige a qué afecta la página: en FX, la capa cuya mezcla lleva K4; en ENV, de quién es la envolvente; en LFO, el destino. (En ARP, el ciclo; en GLO, encadenar; en HOME, cuadrar.)

**Los sonidos del pad** (GLO, K3), a la manera de Barullo Xuno y Barullo 7FM. El pad es parafónico, como un Juno con un solo filtro: cada voz es un DCO (sierra y pulso sin aliasing, PWM y sub una octava abajo), pasan juntas por un filtro resonante que se abre con cada acorde y por el chorus de dos BBD del Juno-106 (I o II), que lo abre en estéreo. CUERDAS (chorus I), NÉBOA (sierra, PWM y sub, chorus II), METAIS (el filtro se abre al atacar), PIANO EL (FM de 7FM: cuerpo 1:1 que ladra al golpe y el diente 14:1, una octava abajo, vuelve a tocar en cada compás), ÓRGANO (pulso y sub), SINTE 80 (pulso estrecho, resonancia, filtro que pica), CAMPANA (FM 3,5:1) y CRISTAL. El arpegio y el rasgueo son FM 2:1 (brillan al golpe), y el bajo, sierra y seno por un filtro que se abre con cada nota.

Los tres LFO son generales: cada uno mueve lo que le digas, y dos pueden ir al mismo sitio. Destinos: pad (filtro, volumen, afinación, paneo, desafine), arpegio (volumen, afinación, paneo, brillo, largo de las notas), bajo (volumen, afinación, brillo, largo), delay (mezcla, tiempo: efecto cinta), delay libre (tiempo), reverb (mezcla), ritmo (volumen, charles), volumen total y agudos de todo (un filtro que los cierra). PAD CHORUS mueve la profundidad del chorus.

### Más controles

| Control | Qué hace |
| --- | --- |
| SELECT | Tempo (40–220 BPM) |
| REC | Graba una idea: empieza con el primer acorde que toques; REC otra vez la cierra (a compases enteros) y la pone en bucle |
| PLAY | Reproduce o para la idea del banco. Mientras suena, lo que toques en directo manda |
| SEQ | Banco siguiente (8 bancos, cada uno con su idea). Si algo suena, el cambio entra al acabar el bucle |
| OCT− / OCT+ | Octava (−2 a +2) |
| SAVE | Guarda los ajustes y los 8 bancos |

Los modificadores se combinan: `7` + `9` da maj9, m9 o 9 según el grado; `sus4` + `7` da 7sus4.

**MIDI de salida** (USB y Bluetooth): rasgueo en el canal 1, acorde en el 2, bajo en el 3 (también el pulsado), arpegio en el 4; con el arpegiador o el bajo pulsado encendidos, reloj MIDI con start/stop.

## Sacar las ideas al ordenador (MIDI y Ableton Live)

Las ideas se guardan con SAVE (y sobreviven a las actualizaciones del firmware). Lo cómodo: con el FM-1 conectado por
USB, Barullín encendido y parado, **doble clic en `Sacar ideas.command`** (en el Mac; la primera vez prepara Python,
y abre la carpeta al acabar). O, a mano:

```sh
source .venv/bin/activate                       # (mido y python-rtmidi, como para instalar)
python3 tools/barullin_export.py
```

Sale la carpeta **Barullin Export**:

- **Idea N - … Project/Idea N - … .mid** (con su txt de acordes al lado): cada idea en su MIDI, con ACORDES, BAJO, ARPEGIO y BATERÍA en pistas y canales separados
  (2, 3, 4 y 10), su tempo y su compás. Es lo que toca el FM-1 con PLAY: el mismo motor de acordes, el bajo
  (sostenido o pulsado), el arpegiador y el ritmo, con los ajustes de cada idea. `--vueltas 4` las repite.
- **Idea N - … Project/Idea N - ….als**: un proyecto de Ableton Live 12 **por idea**, con una pista por parte y la
  idea en la primera escena, con su tempo y su compás. Las pistas van sin instrumento: arrastra el que quieras a cada
  una. `--todas` hace además uno con todas (una escena por idea).
- **FM-1 (original)**: en cada proyecto, una pista de audio con la idea tal como suena en el FM-1 (pad, bajo, arpegio,
  ritmo, efectos y LFO), grabada del mismo firmware en el simulador del ordenador, una vuelta en bucle, para saber de
  qué partes. `--sin-original` la quita.
- **Barullin.barullin**: la copia de seguridad de todo. `python3 base/tools/fm1_backup.py restore "Barullin
  Export/Barullin.barullin"` la devuelve al FM-1 (pregunta en su pantalla: SEL sí, HOME no).

**Con los plugins de Barullo, solo**: de serie, cada proyecto sale con Barullo Xuno, 7FM y Bombo cargados, un kit de
Ableton para la caja y el charles, y Barullo Verb y Ley en los retornos (el molde es `tools/ableton/barullo.als`). El
sonido de cada parte se elige solo, según el que tenía la idea en el FM-1: el pad con el más parecido de Xuno (Cordas,
Néboa, Metais, Órgano, Fondo 1984) o de 7FM (P.ELEC 1, CAMPAS TUB, CELESTA), el bajo con Baixo Barullo, el arpegio
con Arpexo y el bombo con Barullo Bombo.

**Con otros sonidos** (Barullo Xuno, 7FM, Bombo… o lo que sea): en Ableton, haz un Live Set con pistas MIDI llamadas
**Acordes**, **Bajo**, **Arpegio** y **Batería**, ponle a cada una su instrumento y sus efectos (y, si quieres, efectos
en los retornos: Barullo Verb, Barullo Ley), y guárdalo. Luego `--plantilla RUTA/AL/SET.als` (se recuerda para las
siguientes veces): cada proyecto sale con esas pistas tal cual, con sus sonidos, y los clips de la idea dentro. La
batería va en notas de General MIDI (bombo C1, caja D1, charles F#1, abierto A#1): un Drum Rack con algo en esas
casillas.

`--desde FICHERO.barullin` exporta de una copia ya hecha, sin el FM-1. Hace falta un compilador de C (en el Mac,
`xcode-select --install`), porque usa el motor de acordes del firmware tal cual.

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
tools/          barullin_export.py: las ideas a MIDI y a Ableton Live (y su plantilla)
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
- [x] Caja de ritmos sencilla (EDIT)
- [x] Grabar y repetir ideas (REC / PLAY)
- [x] Reloj MIDI y start/stop
- [x] Bajo pulsado, envolventes y tres LFO con 21 destinos
- [x] Delay de rebote al tempo, delay libre en ms y reverb
- [x] Ocho sonidos de pad: DCO, filtro y chorus tipo Juno (de Barullo Xuno) y FM (de Barullo 7FM)

## Licencia

AGPL-3.0-or-later. fm1-base es MPL-2.0 (ver `base/LICENSING.md`).
