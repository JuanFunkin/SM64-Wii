# Super Mario 64 para Wii (homebrew) - basado en sm64-port

## Como compilar (GitHub)
1. Sube TODO este repositorio a GitHub (la carpeta ya trae los cambios aplicados).
2. Pestana Actions -> "Build SM64 Wii (homebrew)" -> Run workflow.
3. Descarga el artefacto `sm64wii.zip` y copia `apps/sm64wii` a la SD (o USB).

## En la SD
apps/sm64wii/boot.dol, assets.tbl, meta.xml
apps/sm64wii/sm64/        <- pon aqui tu ROM de SM64 **US** (.z64/.v64/.n64, cualquier nombre; 8 MB)
apps/sm64wii/extracted/   <- se crea y llena solo (primera vez)
apps/sm64wii/saves/       <- partidas

Primer arranque: barra "Leyendo ROM" -> "Compilando" (extrae texturas, skyboxes, sonido) -> "Cargando" -> juego.
Siguientes arranques: solo "Cargando".

## Idioma / Language
Ingles por defecto; espanol si la Wii esta en espanol. Para forzarlo crea `lang.txt` junto a boot.dol con `en` o `es`.
English by default; Spanish if the Wii system language is Spanish. To force it, create `lang.txt` next to boot.dol containing `en` or `es`.

## Controles
- Wiimote + Nunchuk: stick = mover, A = saltar, B = golpear, Z = agacharse/ground pound, C = camara (R),
  cruz = botones C, + = Start.   Classic Controller y mando GameCube tambien (se suman a Wiimote).
- Wiimote solo (de lado): cruz = mover, 2 = saltar, 1 = golpear, A = Z, B = R, + = Start.
- Jugador 1 = Wiimote/mando 1, Jugador 2 = Wiimote/mando 2.   HOME (o Start+L+R en GC) = salir.

## Jugador 2
Segundo Mario con ropa verde (las luces rojas del modelo se cambian a verde al dibujarlo).
Sin camara propia (sigue al jugador 1). Si se aleja mas de ~5000 unidades, cae al vacio o muere, se teletransporta al jugador 1.
Puede coger estrellas (al recogerla el nivel termina para ambos), abrir puertas, usar tuneles/warps y pinturas, como el jugador 1.
Estrellas, monedas, llaves y vidas se comparten. Solo la muerte y el "suelo de warp" siguen siendo exclusivos del jugador 1.

## Notas / limitaciones conocidas
- Solo ROM US (NTSC-U). - Demos de la pantalla de titulo desactivadas (datos en cero).
- Esto se escribio sin poder compilar para Wii: es probable que haya errores en la primera compilacion; pasalos tal cual.
