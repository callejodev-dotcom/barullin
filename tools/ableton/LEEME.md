# La plantilla de Ableton Live

`plantilla.als` es un Live Set de Ableton Live 12.2.6 (un esqueleto: dos pistas MIDI, dos de audio y dos de retorno,
Reverb y Delay); `clip.xml`, un clip MIDI vacío del mismo Live, de molde para los clips. `tools/barullin_export.py` lo usa de molde: quita las pistas, deja los retornos y
pone una pista MIDI limpia por parte (acordes, bajo, arpegio, batería) con un clip por idea, una escena por idea.

Viene de las pruebas de [abletoolz](https://github.com/elixirbeats/abletoolz)
(`test/version_fixtures/skeletons/12.2.6.als`), bajo la licencia MPL-2.0.

Hace falta Ableton Live 12 para abrir el proyecto que sale; los MIDI valen para cualquier programa. Si tu Live no lo
abre, guarda con él un Live Set vacío (con una pista MIDI) y úsalo de molde:
`python3 tools/barullin_export.py --plantilla MiPlantilla.als`.
