#pragma once

inline constexpr const char* kSchema = R"({
  "vars": {
    "bond": {"type": "range", "min": 0, "max": 1, "default": 0.25},
    "drift": {"type": "range", "min": 0, "max": 1},
    "capacity": {"type": "counter", "min": 1, "max": 6, "default": 3},
    "attends": {"type": "counter", "min": 0, "max": 1000000},
    "met": {"type": "flag"},
    "location_tag": {"type": "set"}
  }
})";

inline constexpr const char* kStorylets = R"({
  "storylets": [
    {"id": "coffee", "when": {"all": ["bond >= 0.2", {"location_tag": "home"}, {"not_played": "coffee"}]},
     "priority": 40, "cooldown_seconds": 1200, "delivery": "walking_talk",
     "lines": [{"speaker": "a", "text_id": "coffee_a", "voice_id": "coffee_a", "seconds": 2}, {"speaker": "b", "text_id": "coffee_b", "seconds": 2}],
     "effects": ["bond += 0.05", {"remember": "habit.coffee"}]},
    {"id": "hello", "when": "bond >= 0.2", "priority": 10, "cooldown_seconds": 5,
     "lines": [{"text_id": "hello_a", "seconds": 1}], "effects": ["attends += 1"]},
    {"id": "once_only", "when": {"any": ["met", {"not": "bond > 0.1"}]}, "priority": 5, "once": true,
     "lines": [{"text_id": "o", "seconds": 0.5}], "effects": ["met = false"]},
    {"id": "tie_a", "when": "attends >= 100", "priority": 7, "lines": [{"text_id": "ta", "seconds": 0.1}]},
    {"id": "tie_b", "when": "attends >= 100", "priority": 7, "lines": [{"text_id": "tb", "seconds": 0.1}]}
  ]
})";
