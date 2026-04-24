function lovr.conf(t)
  t.identity = 'test'
  -- The engine `--headless` flag (parsed in etc/nogame/arg.lua) already disables graphics/audio/
  -- headset/window. We just opt out of the window unconditionally so headed runs still don't open
  -- a window for the test harness.
  t.window = nil
end
