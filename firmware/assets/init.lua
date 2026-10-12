local Window = require "termlet.window"
local Buffer = require "termlet.buffer"

local w = Window.new_main(1)
w.enabled = true
w:update()

local buf = Buffer.new()
buf:write [[
Lorem ipsum dolor sit amet consectetur adipiscing elit. Quis

que faucibus ex sapien vitae pellentesque sem placerat. In i
d cursus mi pretium tellus duis convallis. Tempus leo eu aen
ean sed diam urna tempor. Pulvinar vivamus fringilla lacus n
ec metus bibendum egestas. Iaculis massa nisl malesuada laci
nia integer nunc posuere. Ut hendrerit semper vel class apte
nt taciti sociosqu. Ad litora torquent per conubia nostra in
ceptos himenaeos.

Lorem ipsum dolor sit amet consectetur adipiscing elit. Quis

que faucibus ex sapien vitae pellentesque sem placerat. In i
d cursus mi pretium tellus duis convallis. Tempus leo eu aen
ean sed diam urna tempor. Pulvinar vivamus fringilla lacus n
ec metus bibendum egestas. Iaculis massa nisl malesuada laci
nia integer nunc posuere. Ut hendrerit semper vel class apte
nt taciti sociosqu. Ad litora torquent per conubia nostra in
ceptos himenaeos.

Lorem ipsum dolor sit amet consectetur adipiscing elit. Quis

que faucibus ex sapien vitae pellentesque sem placerat. In i
d cursus mi pretium tellus duis convallis. Tempus leo eu aen
ean sed diam urna tempor. Pulvinar vivamus fringilla lacus n
ec metus bibendum egestas. Iaculis massa nisl malesuada laci
nia integer nunc posuere. Ut hendrerit semper vel class apte
nt taciti sociosqu. Ad litora torquent per conubia nostra in
ceptos himenaeos.

Lorem ipsum dolor sit amet consectetur adipiscing elit. Quis

que faucibus ex sapien vitae pellentesque sem placerat. In i
d cursus mi pretium tellus duis convallis. Tempus leo eu aen
ean sed diam urna tempor. Pulvinar vivamus fringilla lacus n
ec metus bibendum egestas. Iaculis massa nisl malesuada laci
nia integer nunc posuere. Ut hendrerit semper vel class apte
nt taciti sociosqu. Ad litora torquent per conubia nostra in
ceptos himenaeos.

Lorem ipsum dolor sit amet consectetur adipiscing elit. Quis

que faucibus ex sapien vitae pellentesque sem placerat. In i
d cursus mi pretium tellus duis convallis. Tempus leo eu aen
ean sed diam urna tempor. Pulvinar vivamus fringilla lacus n
ec metus bibendum egestas. Iaculis massa nisl malesuada laci
nia integer nunc posuere. Ut hendrerit semper vel class apte
nt taciti sociosqu. Ad litora torquent per conubia nostra in
ceptos himenaeos.

Lorem ipsum dolor sit amet consectetur adipiscing elit. Quis

que faucibus ex sapien vitae pellentesque sem placerat. In i
d cursus mi pretium tellus duis convallis. Tempus leo eu aen
ean sed diam urna tempor. Pulvinar vivamus fringilla lacus n
ec metus bibendum egestas. Iaculis massa nisl malesuada laci
nia integer nunc posuere. Ut hendrerit semper vel class apte
nt taciti sociosqu. Ad litora torquent per conubia nostra in
ceptos himenaeos.

Lorem ipsum dolor sit amet consectetur adipiscing elit. Quis

que faucibus ex sapien vitae pellentesque sem placerat. In i
d cursus mi pretium tellus duis convallis. Tempus leo eu aen
ean sed diam urna tempor. Pulvinar vivamus fringilla lacus n
ec metus bibendum egestas. Iaculis massa nisl malesuada laci
nia integer nunc posuere. Ut hendrerit semper vel class apte
nt taciti sociosqu. Ad litora torquent per conubia nostra in
ceptos himenaeos.

Lorem ipsum dolor sit amet consectetur adipiscing elit. Quis

que faucibus ex sapien vitae pellentesque sem placerat. In i
d cursus mi pretium tellus duis convallis. Tempus leo eu aen
ean sed diam urna tempor. Pulvinar vivamus fringilla lacus n
ec metus bibendum egestas. Iaculis massa nisl malesuada laci
nia integer nunc posuere. Ut hendrerit semper vel class apte
nt taciti sociosqu. Ad litora torquent per conubia nostra in
ceptos himenaeos.

Lorem ipsum dolor sit amet consectetur adipiscing elit. Quis

que faucibus ex sapien vitae pellentesque sem placerat. In i
d cursus mi pretium tellus duis convallis. Tempus leo eu aen
ean sed diam urna tempor. Pulvinar vivamus fringilla lacus n
ec metus bibendum egestas. Iaculis massa nisl malesuada laci
nia integer nunc posuere. Ut hendrerit semper vel class apte
nt taciti sociosqu. Ad litora torquent per conubia nostra in
ceptos himenaeos.

Lorem ipsum dolor sit amet consectetur adipiscing elit. Quis

que faucibus ex sapien vitae pellentesque sem placerat. In i
d cursus mi pretium tellus duis convallis. Tempus leo eu aen
ean sed diam urna tempor. Pulvinar vivamus fringilla lacus n
ec metus bibendum egestas. Iaculis massa nisl malesuada laci
nia integer nunc posuere. Ut hendrerit semper vel class apte
nt taciti sociosqu. Ad litora torquent per conubia nostra in
ceptos himenaeos.
]]

buf:attach(w)

local d = Window.new_dialog(0, 10, 2)
d.enabled = true
d:update()

local dbuf = Buffer.new()
dbuf:write "Hello World"
dbuf:attach(d)
