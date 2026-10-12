local Buffer = {}
Buffer.__index = Buffer

function Buffer.new(capacity)
	local self = setmetatable({
		lines = {},
		capacity = capacity or 1000,

		width = 0,
		height = 0,
		head = 1,
		count = 1,
		scroll = 0,
		hscroll = 0,

		fg = 0xffffff,
		bg = 0x000000,
	}, Buffer)

	self.lines[1] = ""
	return self
end

local function physical_index(self, logical)
	return ((self.head + logical - 2) % self.capacity) + 1
end

function Buffer:setcolor(fg, bg)
	self.fg = fg
	self.bg = bg
end

function Buffer:_newline()
	if self.count < self.capacity then
		self.count = self.count + 1

		local i = physical_index(self, self.count)
		self.lines[i] = ""
	else
		-- oudste regel overschrijven
		self.head = (self.head % self.capacity) + 1

		local i = physical_index(self, self.count)
		self.lines[i] = ""
	end

	if self.scroll > 0 then
		self.scroll = math.min(self.scroll + 1, self.count - 1)
	end
end

function Buffer:write(...)
	for n = 1, select("#", ...) do
		local s = tostring(select(n, ...))

		local pos = 1

		while true do
			local nl = s:find("\n", pos, true)

			local i = physical_index(self, self.count)

			if not nl then
				self.lines[i] = self.lines[i] .. s:sub(pos)
				break
			end

			self.lines[i] = self.lines[i] .. s:sub(pos, nl - 1)
			self:_newline()

			pos = nl + 1
		end
	end

	return self
end

function Buffer:_render_line(window, y, line)
	-- hscroll causes the viewport to move, so shifting the start also works
	local x = -self.hscroll

	for _, cp in utf8.codes(line) do
		if x >= self.width then
			break
		end

		if x >= 0 then
			window:draw_char(x, y, {
				char = cp,
				fg = self.fg,
				bg = self.bg,
			})
		end

		x = x + 1
	end

	while x < self.width do
		if x >= 0 then
			window:draw_char(x, y, {
				char = " ",
				fg = self.fg,
				bg = self.bg,
			})
		end
		x = x + 1
	end
end

function Buffer:scroll_by(delta)
	self.scroll = math.max(0, math.min(self.scroll + delta, self.count - 1 - self.height))
	return self
end

function Buffer:hscroll_by(delta)
	self.hscroll = math.max(0, self.hscroll + delta)
	return self
end

function Buffer:attach(window)
	window:on("render", function(rect)
		local bounds = window.chars_bounds
		self.width = bounds.x1 - bounds.x0
		self.height = bounds.y1 - bounds.y0

		local bottom = self.count - self.scroll
		local top = math.max(1, bottom - self.height + 1)

		for y = 0, self.height do
			local logical = top + y

			local line = ""
			if logical >= 1 and logical <= self.count then
				line = self.lines[physical_index(self, logical)] or ""
			end

			self:_render_line(window, y, line, 0)
		end
	end)

	window:on("pointer", function(event)
		self:scroll_by(event.dscroll)
		self:hscroll_by(event.dhscroll)
	end)
end

return Buffer
