with open("jni_stubs.c", "w") as f:
    f.write("#include \"so_util.h\"\n")
    f.write("extern void game_log(const char *fmt, ...);\n\n")
    for i in range(300):
        f.write(f"void* dummy_jni_{i}() {{ game_log(\"[FakeJNI] Dummy %d called!\\n\"); return (void*)0; }}\n" % i)
    f.write("\nvoid* fake_env_vtable[300] = {\n")
    for i in range(300):
        f.write(f"    (void*)dummy_jni_{i},\n")
    f.write("};\n")
