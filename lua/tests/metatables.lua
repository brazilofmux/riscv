-- __tostring
local mt = {__tostring = function(t) return "MyObj(" .. t.val .. ")" end}
local obj = setmetatable({val = 42}, mt)
print(tostring(obj))

-- __index (function)
local defaults = {color = "red", size = 10}
local mt2 = {__index = function(t, k) return defaults[k] end}
local obj2 = setmetatable({color = "blue"}, mt2)
print(obj2.color)
print(obj2.size)

-- __index (table)
local base = {greet = function() return "hello" end}
local mt3 = {__index = base}
local obj3 = setmetatable({}, mt3)
print(obj3.greet())

-- __newindex
local log = {}
local mt4 = {__newindex = function(t, k, v) log[#log+1] = k .. "=" .. tostring(v) end}
local obj4 = setmetatable({}, mt4)
obj4.x = 1
obj4.y = 2
print(log[1])
print(log[2])

-- __add
local Vec = {}
Vec.__index = Vec
function Vec.new(x, y) return setmetatable({x=x, y=y}, Vec) end
function Vec.__add(a, b) return Vec.new(a.x+b.x, a.y+b.y) end
function Vec:__tostring() return "(" .. self.x .. "," .. self.y .. ")" end
local v = Vec.new(1,2) + Vec.new(3,4)
print(tostring(v))
