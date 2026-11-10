# Shaders do R2BOY

O menu Display > Shaders oferece None, Gauss Scanlines, HQ2, CRT, CRT 3D e
ScaleFX. Eles usam a pipeline experimental portada do R2NESV2. O processamento
é aplicado ao framebuffer 160x144 depois da paleta DMG e do filtro de
scanlines existente, sem alterar o framebuffer do núcleo, e antes da interface.
O tamanho de saída acompanha a área de imagem e a densidade de pixels do SDL.

No Windows, os shaders executam em Direct3D 11 e o resultado é copiado de volta
para uma textura do renderer SDL. Essa cópia pode afetar o desempenho. O backend
tenta a GPU e depois WARP; erros são informados e a imagem normal é restaurada.
Essa textura é apresentada sem mistura de alfa: shaders como ScaleFX retornam
RGB com alfa zero, mas a imagem do jogo deve continuar opaca. O teste de menu
confere que ScaleFX permanece visível tanto em DMG quanto em CGB.
No Linux, os presets ainda não têm backend; ao selecioná-los, é informado que
o recurso não está disponível e o emulador continua com a imagem normal.

O preset Gauss usa a mesma cadeia NTSC do NES, com o primeiro passe ajustado de
1024 para 640 pixels (4 x 160). HQ2 usa uma LUT, CRT corresponde ao Hyllian,
CRT 3D ao Lottes, e ScaleFX usa cinco passes e uma cópia para a área visível.
Os efeitos são os mesmos do R2NESV2 e ainda precisam de avaliação visual em
jogos DMG e CGB, especialmente em tela cheia e em alta densidade de pixels.
Os recursos e suas licenças são copiados para `bin/shaders` durante o build.

Os testes `ntsc_shader`, `hq2_shader`, `scalefx_shader`, `crt_shader`,
`crt3d_shader` e `shader_menu_tests` verificam os efeitos e a seleção no menu
no Windows. Os testes não substituem medição de FPS ou validação visual.
