-- pcall success
local ok, val = pcall(function() return 42 end)
print(ok, val)

-- pcall failure
local ok2, err = pcall(function() error("oops") end)
print(ok2)
print(type(err) == "string")

-- xpcall with message handler
local ok3, msg = xpcall(
    function() error("fail") end,
    function(e) return "caught: " .. e end
)
print(ok3)
-- msg contains "caught: ..." with source info
print(msg:sub(1, 7))

-- Nested pcall
local ok4, val4 = pcall(function()
    local ok5, val5 = pcall(function() error("inner") end)
    return ok5
end)
print(ok4, val4)

-- Error with non-string
local ok6, val6 = pcall(function() error(123) end)
print(ok6, val6)
