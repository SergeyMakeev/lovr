function lovr.arg(arg)
  local options = {
    _help = { short = '-h', long = '--help', help = 'Show help and exit' },
    _version = { short = '-v', long = '--version', help = 'Show version and exit' },
    console = { long = '--console', help = 'Attach Windows console' },
    debug = { long = '--debug', help = 'Enable debugging checks and logging' },
    simulator = { long = '--simulator', help = 'Force headset simulator' },
    watch = { short = '-w', long = '--watch', help = 'Watch files and restart on change' },
    fatalErrors = { long = '--fatal-errors', help = 'Uncaught Lua error: no interactive overlay; exit with error code immediately' },
    _logFile = { long = '--log-file=PATH', help = 'Redirect stdout/stderr (full process log) to PATH; best for CI' },
    _runFrames = { long = '--run-frames=N', help = 'Exit with 0 after N frames' },
    noVsync = { long = '--no-vsync', help = 'Disable vsync (conf.graphics.vsync = false)' }
  }

  do
    local i = 1
    while i <= #arg do
      local a = arg[i]
      local lf = type(a) == 'string' and a:match('^%-%-log%-file=(.*)$')
      local rf = type(a) == 'string' and a:match('^%-%-run%-frames=(%d+)$')
      if lf then
        arg.logFile = lf ~= '' and lf or nil
        table.remove(arg, i)
      elseif rf then
        arg.runFrames = tonumber(rf)
        table.remove(arg, i)
      else
        i = i + 1
      end
    end
  end

  local shift

  for i, argument in ipairs(arg) do
    if argument:match('^%-') then
      for name, option in pairs(options) do
        if argument == option.short or argument == option.long then
          arg[name] = true
          break
        end
      end
    else
      shift = i
      break
    end
  end

  shift = shift or (#arg + 1)

  for i = 0, #arg do
    arg[i - shift], arg[i] = arg[i], nil
  end

  if arg.console or arg._help or arg._version then
    local ok, system = pcall(require, 'lovr.system')
    if ok and system then system.openConsole() end
  end

  if arg._help then
    local message = {}

    local list = {}
    for name, option in pairs(options) do
      option.name = name
      table.insert(list, option)
    end

    table.sort(list, function(a, b) return a.name < b.name end)

    for i, option in ipairs(list) do
      if option.short and option.long then
        table.insert(message, ('  %s, %s\t\t%s'):format(option.short, option.long, option.help))
      else
        table.insert(message, ('  %s\t\t%s'):format(option.long or option.short, option.help))
      end
    end

    table.insert(message, 1, 'usage: lovr [options] [<source>]\n')
    table.insert(message, 2, 'options:')
    table.insert(message, '\n<source> can be a Lua file, a folder, or a zip archive')
    print(table.concat(message, '\n'))
    return 'quit'
  end

  if arg._version then
    if select('#', lovr.getVersion()) >= 5 then
      print(('LOVR %d.%d.%d (%s) %s'):format(lovr.getVersion()))
    else
      print(('LOVR %d.%d.%d (%s)'):format(lovr.getVersion()))
    end
    return 'quit'
  end

  return function(conf)
    if arg.debug then
      conf.audio.debug = true
      conf.graphics.debug = true
      conf.headset.debug = true
    end

    if arg.simulator then
      conf.headset.connect = false
      conf.headset.start = false
    end

    if arg.watch then
      lovr.filesystem.watch()
    end

    conf.test = conf.test or {}
    if arg.logFile then conf.test.logFile = arg.logFile end
    if arg.runFrames then conf.test.runFrames = arg.runFrames end
    if arg.fatalErrors then
      conf.test.fatalErrors = true
      conf.test.interactiveErrors = false
    end

    if arg.noVsync and conf.graphics then
      conf.graphics.vsync = false
    end
  end
end
