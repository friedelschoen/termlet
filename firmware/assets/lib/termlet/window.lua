local native = require("termlet._core.window")

local Window = {}
Window.__index = Window

local function new(handle)
	return setmetatable({
		_handle = handle,
	}, Window)
end

function Window.new_main(z_index)
	return new(native.newmain(z_index))
end

function Window.new_dialog(z_index, width, height)
	return new(native.newdialog(z_index, width, height))
end

function Window.new_clip(z_index, edge, size)
	return new(native.newclip(z_index, edge, size))
end

function Window.new_overlay(z_index, edge, size)
	return new(native.newoverlay(z_index, edge, size))
end

function Window:on(event, fn)
	if event == "layout" then
		native.setlayouthandler(self._handle, fn)
	elseif event == "render" then
		native.setrenderhandler(self._handle, fn)
	else
		error("unsupported window event: " .. tostring(event), 2)
	end

	return self
end

function Window:remove()
	native.remove(self._handle)
	self._handle = nil
end

function Window:enable(en)
	native.enable(self._handle, en)
	return self
end

function Window:resize(width, height)
	native.resize(self._handle, width, height)
	return self
end

function Window:draw_char(x, y, char)
	native.drawchar(self._handle, x, y, char)
	return self
end

return Window
