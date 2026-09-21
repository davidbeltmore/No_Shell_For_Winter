"""Read-only probe for the protected Calysto Shape PCG graph signature."""

import unreal


GRAPH_PATH = "/Game/Calysto/Dungeon/PCG/PCG_MassiveDungeonShape"


def safe_property(owner, name):
    try:
        return owner.get_editor_property(name)
    except Exception as exc:
        return "<unavailable: {}>".format(exc)


def safe_attribute(owner, name):
    try:
        value = getattr(owner, name)
        return value() if callable(value) else value
    except Exception as exc:
        return "<unavailable: {}>".format(exc)


def pin_summary(pin):
    properties = safe_property(pin, "properties")
    label = safe_property(properties, "label") if not isinstance(properties, str) else properties
    edges = safe_property(pin, "edges")
    try:
        edge_count = len(list(edges))
    except Exception:
        edge_count = -1
    return {
        "name": pin.get_name(),
        "label": str(label),
        "edge_count": edge_count,
        "api": [
            value for value in dir(pin)
            if "edge" in value.lower() or "connect" in value.lower() or "propert" in value.lower()
        ],
    }


graph = unreal.load_asset(GRAPH_PATH)
if graph is None:
    raise RuntimeError("Unable to load " + GRAPH_PATH)

unreal.log("CODEX_CALYSTO_V6_PCG_GRAPH=" + graph.get_path_name())
unreal.log("CODEX_CALYSTO_V6_PCG_GRAPH_CLASS=" + graph.get_class().get_path_name())
unreal.log("CODEX_CALYSTO_V6_PCG_GRAPH_API=" + repr([
    value for value in dir(graph)
    if "node" in value.lower() or "pin" in value.lower()
]))

nodes = safe_property(graph, "nodes")
unreal.log("CODEX_CALYSTO_V6_PCG_NODES_VALUE=" + repr(nodes))
try:
    node_values = list(nodes)
except Exception:
    node_values = []
unreal.log("CODEX_CALYSTO_V6_PCG_NODE_COUNT=" + str(len(node_values)))
if node_values:
    for node in node_values:
        if node is None:
            continue
        settings = safe_attribute(node, "get_settings")
        title = safe_attribute(node, "node_title")
        settings_class = (
            settings.get_class().get_path_name()
            if hasattr(settings, "get_class") else repr(settings)
        )
        input_pins = list(safe_attribute(node, "input_pins"))
        output_pins = list(safe_attribute(node, "output_pins"))
        input_summaries = [pin_summary(pin) for pin in input_pins]
        output_summaries = [pin_summary(pin) for pin in output_pins]
        searchable = "{} {} {} {} {}".format(
            node.get_name(), title, settings_class, input_summaries, output_summaries
        ).lower()
        if not any(value in searchable for value in (
            "reroutedeclaration", "main", "room", "door", "path", "start", "end",
            "matchandsetattributes_26", "mergepoints_48",
        )):
            continue
        unreal.log("CODEX_CALYSTO_V6_PCG_NODE name={} class={} title={} settings_class={} input_pins={} output_pins={} settings_api={}".format(
            node.get_name(),
            node.get_class().get_path_name(),
            repr(str(title)),
            settings_class,
            repr(input_summaries),
            repr(output_summaries),
            repr([
                value for value in dir(settings)
                if "name" in value.lower() or "tag" in value.lower() or "declaration" in value.lower()
            ]),
        ))

unreal.log("CODEX_CALYSTO_V6_PCG_SIGNATURE_DONE")
