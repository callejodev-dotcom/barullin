# La plantilla de Ableton Live

`plantilla.als` es un Live Set vacío guardado con Ableton Live 12.4.16 (dos pistas MIDI, dos de audio y dos de
retorno, Reverb y Delay). `tools/barullin_export.py` lo usa de molde: quita las pistas, deja los retornos y pone una
pista MIDI limpia por parte (acordes, bajo, arpegio, batería) con un clip por idea, una escena por idea. Lo escribe
como lo escribe Live (el JSON de los atributos entre comillas simples).

`clip.xml` es un clip MIDI vacío de molde para los clips. Viene de las pruebas de
[abletoolz](https://github.com/elixirbeats/abletoolz) (`test/version_fixtures/skeletons/12.4.5b.als`), MPL-2.0.

Si tu Live no abre el proyecto que sale, guarda con él un Live Set vacío (con una pista MIDI) y úsalo de molde:
`python3 tools/barullin_export.py --plantilla MiPlantilla.als`. Los MIDI valen para cualquier programa.
