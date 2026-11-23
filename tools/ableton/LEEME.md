# La plantilla de Ableton Live

`plantilla.als` es un Live Set vacío de Ableton Live 12.4 (un esqueleto: una pista MIDI con un clip, dos de audio y
dos de retorno, Reverb y Delay). `tools/barullin_export.py` lo usa de molde: quita las pistas, deja los retornos y
pone una pista MIDI limpia por parte (acordes, bajo, arpegio, batería) con un clip por idea, una escena por idea.

Viene de las pruebas de [abletoolz](https://github.com/elixirbeats/abletoolz)
(`test/version_fixtures/skeletons/12.4.5b.als`), bajo la licencia MPL-2.0.

Hace falta Ableton Live 12 o posterior para abrir el proyecto que sale; los MIDI valen para cualquier programa.
