-- binds @p obj as self inside @method
local function bind(obj, method)
	return function(...)
		return method(obj, ...)
	end
end

-- pipe ties two streamables together bidirectionally.
local function pipe(left, right)
	left:on("data", bind(right, right.write))
	right:on("data", bind(left, left.write))
end

return {
	bind = bind,
	pipe = pipe,
}
