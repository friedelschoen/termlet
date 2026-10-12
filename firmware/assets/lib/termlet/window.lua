local core = require "termlet._core.window"

local Window = {}

Window.__index = function(self, key)
	local method = rawget(Window, key)
	if method ~= nil then
		return method
	end

	return core.get(self._handle, key)
end

Window.__newindex = function(self, key, value)
	if key == "_handle" then
		rawset(self, key, value)
		return
	end

	core.set(self._handle, key, value)
end

local function new()
	return setmetatable({
		_handle = core.new(),
	}, Window)
end

function Window.new_main(z_index)
	local w = new()
	w.mode = "main"
	w.z_index = z_index
	w:update()
	return w
end

function Window.new_dialog(z_index, width, height)
	local w = new()
	w.mode = "dialog"
	w.z_index = z_index
	w.width = width
	w.height = height
	w:update()
	return w
end

function Window.new_clip(z_index, edge, size)
	local w = new()
	w.mode = "clip"
	w.z_index = z_index
	w.edge = edge
	w.width = size
	w.height = size
	w:update()
	return w
end

function Window.new_overlay(z_index, edge, size)
	w = new()
	w.mode = "overlay"
	w.z_index = z_index
	w.edge = edge
	w.width = size
	w.height = size
	w:update()
	return w
end

function Window:update()
	core.update(self._handle)
end

function Window:remove()
	core.remove(self._handle)
	self._handle = nil
end

function Window:draw_char(x, y, char)
	core.drawchar(self._handle, x, y, char)
	return self
end

function Window:on(event, fn)
	if event ~= "layout" and event ~= "render" and event ~= "key" and event ~= "pointer" then
		error("unsupported window event: " .. tostring(event), 2)
	end

	self[event] = fn
	return self
end

function Window:attach(b)
	self:on("render", function(...)
		b:render(self, ...)
	end)
	self:on("layout", function(...)
		b:layout(self, ...)
	end)
	self:update()
end

return Window
