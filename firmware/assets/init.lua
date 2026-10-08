local Window = require("termlet.window")

local w = Window.new_main(0)

width = 0
height = 0
i = 0

w:on("layout", function(w, h, visible)
	width = w
	height = h
	print(visible)
end)

function draw_at(i, ch)
	local x = i % width
	local y = i // width 
	w:draw_char(x, y, { char = ch })
end

w:on("render", function(rect)
    draw_at(i, ' ')
	i = (i + 1) % (width * height)
    draw_at(i, '@')
end)

w:enable(true)

print(w)
