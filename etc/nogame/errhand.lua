function lovr.errhand(message)
  print('Error:\n\n' .. tostring(message))

  local font = lovr.graphics.newFont('ZhiMaMono-Regular.ttf')
  local headerSize = 32
  local textSize = 24
  local margin = 16
  local padding = 24
  local border = 3
  local borderRadius = 8

  local stack = {}
  local level = 1

  local layout = {}
  local panelIndex = 1
  local stackColumnWidth = 0
  local scroll = { source = { current = 0, target = 0 } }

  local function clamp(x, min, max)
    return math.min(math.max(x, min), max)
  end

  local function setLevel(index)
    local prev = level

    level = clamp(index, 1, #stack)
    frame = stack[level]

    if level ~= prev and stack[level].lines and stack[level].currentline ~= -1 then
      scroll.source.target = math.max((frame.currentline + .5) * textSize - ((layout.source.h - padding) / 2), 0)
      if not prev or not stack[prev] or stack[prev].source ~= stack[level].source then
        scroll.source.current = scroll.source.target
      end
    end
  end

  local function buildVariableTable(frame)
    frame.rows = {}
    frame.columnWidth = 0

    local function halp(name, value, parent, id, depth)
      frame.columnWidth = math.max(frame.columnWidth, font:getWidth(name) * textSize + depth * padding)

      local row = {
        name = name,
        value = value,
        parent = parent,
        index = #frame.rows + 1,
        depth = depth
      }

      table.insert(frame.rows, row)

      if type(value) == 'table' then
        row.id = id .. string.format('%p:%s', name, value)

        if frame.expanded[row.id] then
          for k, v in pairs(value) do
            halp(k, v, row, row.id, depth + 1)
          end
        end
      end
    end

    for i, var in ipairs(frame.variables) do
      halp(var[1], var[2], nil, '', 0)
    end
  end

  if debug then
    for i = 4, 50 do
      local frame = debug.getinfo(i, 'Snufl')

      if not frame then break end

      table.insert(stack, frame)

      frame.variables = {}
      frame.expanded = {}
      frame.rowIndex = 1
      frame.rowScroll = { current = 0, target = 0 }

      -- Pretty name
      if frame.func then
        if frame.name and lovr[frame.name] == frame.func then
          frame.label = 'lovr.' .. frame.name
        elseif frame.what == 'C' then
          for module, value in pairs(lovr) do
            if type(value) == 'table' then
              for name, fn in pairs(value) do
                if fn == frame.func then
                  frame.label = string.format('lovr.%s.%s', module, name)
                end
              end
            end
          end

          for object, methods in pairs(debug.getregistry()) do
            if type(object) == 'string' and object:match('^%u') then
              for name, fn in pairs(methods) do
                if fn == frame.func then
                  frame.label = string.format('%s:%s', object, name)
                end
              end
            end
          end
        end
      end

      frame.label = frame.label or frame.name or '<anonymous>'
      stackColumnWidth = math.max(stackColumnWidth, font:getWidth(frame.label) * textSize)

      -- Locals
      for j = 1, 100 do
        local name, value = debug.getlocal(i, j)

        if not name then
          break
        elseif name:sub(1, 1) ~= '(' then
          table.insert(frame.variables, { name, value })
        end
      end

      -- Upvalues
      for j = 1, 100 do
        local name, value = debug.getupvalue(frame.func, j)

        if not name then
          break
        else
          table.insert(frame.variables, { name, value })
        end
      end

      table.insert(frame.variables, { '_ENV', getfenv(i) })

      -- Source
      if frame.source:sub(1, 1) == '@' then
        local contents = lovr.filesystem.read(frame.source:sub(2))

        if contents then
          frame.lines = {}
          for line in contents:gmatch('([^\n]*)\n') do
            table.insert(frame.lines, line)
          end
        end
      end

      if frame.short_src and frame.currentline ~= -1 then
        frame.short_src = frame.short_src .. ':' .. frame.currentline
      end

      buildVariableTable(frame)
    end

    while stack[#stack].short_src and (stack[#stack].short_src:match('boot.lua') or stack[#stack].short_src:match('[C]')) do
      table.remove(stack)
    end

    while stack[level].what == 'C' do
      level = level + 1
    end

    setLevel(level)
  end

  local function reflow()
    local width, height = lovr.system.getWindowDimensions()

    layout.message = {
      x = padding,
      y = padding + margin,
      w = width - 2 * padding,
      h = #font:getLines(message, (width - 4 * padding) / textSize) * textSize + 2 * padding
    }

    layout.stack = {
      x = padding,
      y = layout.message.y + layout.message.h + padding + margin,
      w = width - 2 * padding,
      h = textSize * #stack + 2 * padding
    }

    local rowY = layout.stack.y + layout.stack.h + padding + margin

    layout.source = {
      x = padding,
      y = rowY,
      w = width / 2 - 1.5 * padding,
      h = height - padding - rowY
    }

    layout.variables = {
      x = width / 2 + padding / 2,
      y = rowY,
      w = width / 2 - 1.5 * padding,
      h = height - padding - rowY
    }
  end

  reflow()

  local function inside(x, y, rect)
    return x >= rect.x and x <= rect.x + rect.w and y >= rect.y and y <= rect.y + rect.h
  end

  local function flerp(a, b, rate, dt)
    return a + (b - a) * (1 - math.exp(-rate * dt))
  end

  local function clampSourceScroll()
    local frame = stack[level]
    scroll.source.target = math.min(scroll.source.target, #frame.lines * textSize - (layout.source.h - 2 * padding))
    scroll.source.target = math.max(scroll.source.target, 0)
  end

  local function clampVariableScroll(showSelected)
    local frame = stack[level]
    local maxScroll = math.max(#frame.rows * textSize - (layout.variables.h - 2 * padding), 0)
    if showSelected then
      frame.rowScroll.target = math.min(frame.rowScroll.target, (frame.rowIndex - 1) * textSize)
      frame.rowScroll.target = math.max(frame.rowScroll.target, math.max(frame.rowIndex * textSize - (layout.variables.h - 2 * padding), 0))
    end
    frame.rowScroll.target = math.min(math.max(frame.rowScroll.target, 0), maxScroll)
  end

  if not lovr.graphics or not lovr.graphics.isInitialized() then
    return function() return 1 end
  end

  if lovr.audio then lovr.audio.stop() end

  if not lovr.headset or lovr.headset.getPassthrough() == 'opaque' then
    lovr.graphics.setBackgroundColor(.11, .10, .14)
  else
    lovr.graphics.setBackgroundColor(0, 0, 0, 0)
  end

  lovr.system.setKeyRepeat(true)

  return function()
    lovr.system.pollEvents()

    local frame = stack[level]

    for name, a, b, c in lovr.event.poll() do
      if name == 'quit' then
        return a or 1
      elseif name == 'restart' then
        return 'restart', lovr.restart and lovr.restart()
      elseif name == 'filechanged'then
        lovr.event.restart()
      elseif name == 'resize' then
        reflow()
      elseif name == 'keypressed' then
        if a == 'f5' then
          lovr.event.restart()
        elseif a == 'escape' then
          lovr.event.quit()
        elseif a == 'j' or a == 'down' then
          if panelIndex == 1 then
            setLevel(level + 1)
          elseif panelIndex == 2 then
            scroll.source.target = scroll.source.target + textSize
            clampSourceScroll()
          elseif panelIndex == 3 then
            frame.rowIndex = math.min(frame.rowIndex + 1, #frame.rows)
            buildVariableTable(frame)
            clampVariableScroll(true)
          end
        elseif a == 'k' or a == 'up' then
          if panelIndex == 1 then
            setLevel(level - 1)
          elseif panelIndex == 2 then
            scroll.source.target = scroll.source.target - textSize
            clampSourceScroll()
          elseif panelIndex == 3 then
            frame.rowIndex = math.max(frame.rowIndex - 1, 1)
            buildVariableTable(frame)
            clampVariableScroll(true)
          end
        elseif a == 'h' or a == 'left' then
          if panelIndex == 3 then
            local row = frame.rows[frame.rowIndex]
            if row then
              if frame.expanded[row.id] then
                frame.expanded[row.id] = false
                buildVariableTable(frame)
                clampVariableScroll(true)
              elseif row.parent and frame.expanded[row.parent.id] then
                frame.rowIndex = row.parent.index
                frame.expanded[row.parent.id] = false
                buildVariableTable(frame)
                clampVariableScroll(true)
              end
            end
          end
        elseif a == 'l' or a == 'right' then
          if panelIndex == 3 then
            local row = frame.rows[frame.rowIndex]
            if row and type(row.value) == 'table' then
              frame.expanded[row.id] = true
              buildVariableTable(frame)
              clampVariableScroll(true)
            end
          end
        elseif (a == 'g' and not lovr.system.isKeyDown('lshift', 'rshift')) or a == 'home' then
          if panelIndex == 3 then
            frame.rowIndex = 1
            clampVariableScroll(true)
          end
        elseif (a == 'g' and lovr.system.isKeyDown('lshift', 'rshift')) or a == 'end' then
          if panelIndex == 3 then
            frame.rowIndex = #frame.rows
            clampVariableScroll(true)
          end
        elseif a == 'tab' then
          panelIndex = 1 + (panelIndex % 3)
        end
      elseif name == 'wheelmoved' then
        local dy = b
        local mx, my = lovr.system.getMousePosition()
        if inside(mx, my, layout.source) then
          scroll.source.target = scroll.source.target - textSize * dy
          clampSourceScroll()
        elseif inside(mx, my, layout.variables) then
          frame.rowScroll.target = frame.rowScroll.target - textSize * dy
          clampVariableScroll()
        end
      elseif name == 'mousepressed' and c == 1 then
        local x, y = a, b
        if inside(x, y, layout.stack) then
          local row = 1 + math.floor((y - padding - layout.stack.y) / textSize)
          if row >= 1 and row <= #stack then
            setLevel(row)
          end
          panelIndex = 1
        elseif inside(x, y, layout.source) then
          panelIndex = 2
        elseif inside(x, y, layout.variables) then
          local index = 1 + math.floor((y + frame.rowScroll.current - padding - layout.variables.y) / textSize)
          if index >= 1 and index <= #frame.rows then
            local row = frame.rows[index]
            frame.rowIndex = index
            if type(row.value) == 'table' then
              frame.expanded[row.id] = not frame.expanded[row.id]
              buildVariableTable(frame)
              clampVariableScroll(true)
            end
          end
          panelIndex = 3
        end
      end
    end

    if lovr.timer then
      local dt = lovr.timer.step()
      scroll.source.current = flerp(scroll.source.current, scroll.source.target, 20, dt)
      frame.rowScroll.current = flerp(frame.rowScroll.current, frame.rowScroll.target, 20, dt)
    else
      scroll.source.current = scroll.source.target
      frame.rowScroll.current = frame.rowScroll.target
    end

    local pass = lovr.system.isWindowOpen() and lovr.graphics.getWindowPass()

    if pass then
      pass:setProjection('orthographic')
      pass:setDepthTest()
      pass:setFont(font)

      local function section(pass, title, layout, focused, callback)
        local x, y, w, h = layout.x, layout.y, layout.w, layout.h
        local titleWidth = font:getWidth(title) * headerSize

        if h <= 2 * padding then return end

        -- Border
        pass:setColor(focused and 0x6747c4 or 0x404040)
        pass:roundrect(x + w / 2, y + h / 2, 0, w + border, h + border, 0, nil, borderRadius, 4)

        -- Background
        pass:setColor(.11, .10, .14)
        pass:roundrect(x + w / 2, y + h / 2, 0, w - border, h - border, 0, nil, borderRadius, 4)
        pass:plane(x + padding + titleWidth / 2, y, 0, titleWidth + padding, headerSize)

        -- Title
        pass:setColor(focused and 0xc0c0c0 or 0x808080)
        pass:text(title, x + padding, y, 0, headerSize, nil, 0, 'left', 'middle')

        -- Contents
        pass:setScissor(x, y + padding, w, h - 2 * padding)
        callback(x, y, w, h)
        pass:setScissor()
      end

      section(pass, 'Error', layout.message, false, function(x, y, w, h)
        pass:setColor(0xf0f0f0)
        pass:text(message, x + padding, y + padding, 0, textSize, nil, (w - 2 * padding) / textSize, 'left', 'top')
      end)

      section(pass, 'Stack', layout.stack, panelIndex == 1, function(x, y, w, h)
        for i, frame in ipairs(stack) do
          if i == level then
            pass:setColor(0x6747c4)
            pass:plane(x + w / 2, y + padding + textSize / 2, 0, w - border, textSize)
          end

          pass:setColor(i == level and 0xffffff or 0x808080)
          pass:text(frame.label, x + padding, y + padding, 0, textSize, nil, 0, 'left', 'top')

          if frame.short_src then
            pass:text(frame.short_src, x + padding + stackColumnWidth + padding, y + padding, 0, textSize, nil, 0, 'left', 'top')
          end

          y = y + textSize
        end
      end)

      section(pass, 'Source', layout.source, panelIndex == 2, function(x, y, w, h)
        if frame.lines and frame.currentline ~= -1 then
          local gutter = font:getWidth(#frame.lines) * textSize + padding / 2
          local maxlines = math.ceil((h - 2 * padding) / textSize)
          local first = math.max(1 + math.floor(scroll.source.current / textSize), 1)
          local last = math.min(first + maxlines, #frame.lines)

          for i = first, last do
            local y = y + (i - 1) * textSize - scroll.source.current
            if i == frame.currentline then
              pass:setColor(0x6747c4)
              pass:plane(x + w / 2, y + padding + textSize / 2, 0, w - border, textSize)
            end
            pass:setColor(i == frame.currentline and 0xffffff or 0x808080)
            pass:text(tostring(i), x + padding, y + padding, 0, textSize, nil, 0, 'left', 'top')
            pass:text(frame.lines[i], x + padding + gutter, y + padding, 0, textSize, nil, 0, 'left', 'top')
          end
        end
      end)

      section(pass, 'Variables', layout.variables, panelIndex == 3, function(x, y, w, h)
        local bottom = y + h + frame.rowScroll.current

        for i, row in ipairs(frame.rows) do
          if i == frame.rowIndex then
            pass:setColor(0x6747c4)
            pass:plane(x + w / 2, y - frame.rowScroll.current + padding + textSize / 2, 0, w - border, textSize)
          end

          pass:setColor(i == frame.rowIndex and 0xffffff or 0xc0c0c0)
          pass:text(row.name, x + padding + row.depth * padding, y - frame.rowScroll.current + padding, 0, textSize, nil, 0, 'left', 'top')

          local value = type(row.value) == 'string' and string.format("'%s'", row.value) or tostring(row.value)
          pass:text(value, x + padding + frame.columnWidth + padding, y - frame.rowScroll.current + padding, 0, textSize, nil, 0, 'left', 'top')
          y = y + textSize

          if y > bottom then
            break
          end
        end
      end)

      lovr.graphics.submit(pass)
      lovr.graphics.present()
    end
  end
end
