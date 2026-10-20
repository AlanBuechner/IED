"""Protocol v1 frame validation against shared/host-api/protocol/frames.v1.schema.json."""

import pathlib

import minischema

REPO = pathlib.Path(__file__).resolve().parents[2]
FRAMES_SCHEMA = REPO / "shared" / "host-api" / "protocol" / "frames.v1.schema.json"


class FrameChecker:
	"""Validates a frame against the definition its 't' selects (the schema's oneOf is discriminated by 't')."""

	def __init__(self, schema_path=FRAMES_SCHEMA):
		registry = minischema.Registry()
		names = [branch["$ref"].rpartition("/")[2] for branch in registry.load(schema_path)["oneOf"]]
		self.by_type = {name: minischema.Validator(registry, schema_path, "/$defs/" + name) for name in names}

	def problems(self, frame):
		validator = self.by_type.get(frame.get("t")) if isinstance(frame, dict) else None
		if validator is None:
			return ["not a frame: unknown 't'"]
		return [str(p) for p in validator.validate(frame)]
