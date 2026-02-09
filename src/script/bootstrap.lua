novaria = novaria or {}
novaria.last_tick = 0
novaria.last_delta = 0
novaria.last_event_name = ""
novaria.last_event_payload = ""

function novaria_on_tick(tick_index, delta_seconds)
  novaria.last_tick = tick_index
  novaria.last_delta = delta_seconds
end

function novaria_on_event(event_name, payload)
  novaria.last_event_name = event_name
  novaria.last_event_payload = payload
end
