def can_build(env, platform):
    # Depend on the core GDScript module so we can reuse its implementation.
    env.module_add_dependencies("hmscript", ["gdscript"], True)
    return True


def configure(env):
    pass


def get_doc_classes():
    # Keep minimal for now; HMScript reuses GDScript implementation.
    return []


def get_doc_path():
    return "doc_classes"

