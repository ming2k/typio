# ── Mozc engine ───────────────────────────────────────────────────────────
# Requires protobuf code generation from mozc_commands.proto

proto_xml = files('proto/mozc_commands.proto')
proto_src = custom_target('mozc-proto',
    output: ['mozc_commands.pb.cc', 'mozc_commands.pb.h'],
    input: proto_xml,
    command: [protoc, '--cpp_out=@OUTDIR@', '-I@CURRENT_SOURCE_DIR@/proto', '@INPUT@'],
)

mozc_sources = files(
    'mozc_engine.cc',
)
