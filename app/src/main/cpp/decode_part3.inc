    for (std::size_t p3 = 2; p3 < vc; ++p3) {
        if (!skip[p3]) {
            const Vec3 e1 = normalize(sub(local_positions[p3], local_positions[p1]));
            const Vec3 e2 = normalize(sub(local_positions[p2], local_positions[p1]));
            const Vec3 face = normalize(cross(e1, e2));
            Vec3 sum = add(add(normals[p1], normals[p2]), normals[p3]);
            Vec3 normal_sum = (sum.x == 0.0f && sum.y == 0.0f && sum.z == 0.0f)
                                  ? face : normalize(sum);
            const auto a = static_cast<std::uint32_t>(base_vertex + p1);
            const auto b = static_cast<std::uint32_t>(base_vertex + p2);
            const auto c = static_cast<std::uint32_t>(base_vertex + p3);
            if (dot(normal_sum, face) > 0.0f) {
                output->indices.push_back(a);
                output->indices.push_back(c);
                output->indices.push_back(b);
            } else {
                output->indices.push_back(a);
                output->indices.push_back(b);
                output->indices.push_back(c);
            }
        }
        p1 = p2;
        p2 = p3;
    }
    return true;
}

DecodeResult decode_model(Format format,
                          const std::uint8_t* bytes,
                          std::size_t size) noexcept {
    try {
        BinaryReader reader(bytes, size);
        if (!reader.range(0, kHeaderSize)) {
            return {DecodeStatus::InvalidData, format, {}, "resource shorter than 0x40 header"};
        }
        const bool magic_ok = format == Format::Scm
            ? exact_magic(reader, 'S', 'C', 'M', ' ')
            : exact_magic(reader, 'M', 'O', 'D', ' ');
        if (!magic_ok) {
            return {DecodeStatus::InvalidData, format, {}, "4-byte magic mismatch"};
        }

        float version = 0.0f;
        std::uint8_t object_count = 0, bone_count = 0;
        std::size_t skeleton_offset = 0;
        if (!reader.read_le(0x04, &version) ||
            !reader.read_le(0x10, &object_count) ||
            !reader.read_le(0x11, &bone_count) ||
            !read_offset(reader, 0x20, &skeleton_offset)) {
            return {DecodeStatus::InvalidData, format, {}, "header fields truncated"};
        }
        if (!supported_version(format, version)) {
            return {DecodeStatus::UnsupportedLayout, format, {}, "unseen SCM/MOD version"};
        }
        if (!reader.table(kHeaderSize, object_count, kObjectStride)) {
            return {DecodeStatus::InvalidData, format, {}, "object table out of bounds"};
        }

        std::vector<ObjectRec> objects;
        objects.reserve(object_count);
        std::size_t mesh_count = 0;
        std::size_t vertex_count = 0;
        for (std::size_t i = 0; i < object_count; ++i) {
            ObjectRec obj{};
            if (!read_object(reader, i, &obj)) {
                return {DecodeStatus::InvalidData, format, {}, "invalid object record or mesh table"};
            }
            if (mesh_count > kMaxMeshes - obj.mesh_count) {
                return {DecodeStatus::TooLarge, format, {}, "mesh count exceeds decoder cap"};
            }
            mesh_count += obj.mesh_count;
            objects.push_back(obj);
        }

        std::vector<Vec3> object_translations(object_count, Vec3{});
        if (format == Format::Scm &&
            !parse_scm_object_translations(reader, skeleton_offset, bone_count,
                                           object_count, &object_translations)) {
            return {DecodeStatus::InvalidData, format, {}, "invalid SCM skeleton/object placement tables"};
        }

        Mesh output;
        for (std::size_t object_index = 0; object_index < objects.size(); ++object_index) {
            const auto& obj = objects[object_index];
            std::size_t object_vertex_sum = 0;
            for (std::size_t m = 0; m < obj.mesh_count; ++m) {
                MeshRec rec{};
                const std::size_t def = obj.mesh_offset + m * kMeshDefStride;
                if (!read_mesh(reader, def, format, &rec)) {
                    return {DecodeStatus::InvalidData, format, {}, "invalid mesh definition or stream bounds"};
                }
                if (vertex_count > kMaxVertices - rec.vertex_count) {
                    return {DecodeStatus::TooLarge, format, {}, "vertex count exceeds decoder cap"};
                }
                vertex_count += rec.vertex_count;
                object_vertex_sum += rec.vertex_count;
                if (!append_mesh(reader, format, rec, object_translations[object_index], &output)) {
                    return {DecodeStatus::InvalidData, format, {}, "mesh stream decode/topology failed"};
                }
            }
            (void)object_vertex_sum;
        }

        if (output.vertices.empty()) {
            return {DecodeStatus::InvalidData, format, {}, "resource contains no decodable vertices"};
        }
        if (output.indices.empty()) {
            return {DecodeStatus::InvalidData, format, {}, "resource contains no decodable triangles"};
        }
        return {DecodeStatus::Ok, format, std::move(output),
                format == Format::Scm ? "SCM corpus-backed mesh decode" : "MOD corpus-backed mesh decode"};
    } catch (const std::bad_alloc&) {
        return {DecodeStatus::TooLarge, format, {}, "decoder allocation failed"};
    } catch (...) {
        return {DecodeStatus::InvalidData, format, {}, "decoder rejected malformed input"};
    }
}

}  // namespace

DecodeResult decode_resource(std::string_view filename,
                             const std::uint8_t* bytes,
                             std::size_t size) noexcept {
    if (size > kMaxResourceBytes) {
        return {DecodeStatus::TooLarge, Format::Unknown, {}, "resource exceeds decoder cap"};
    }
    if (size != 0 && bytes == nullptr) {
        return {DecodeStatus::InvalidData, Format::Unknown, {}, "null byte span"};
    }

    const auto prefix_size = size < 64 ? size : 64;
    const auto p = probe(filename, bytes, prefix_size);
    switch (p.format) {
        case Format::Scm: return decode_model(Format::Scm, bytes, size);
        case Format::Mod: return decode_model(Format::Mod, bytes, size);
        default:
            return {DecodeStatus::UnknownFormat, Format::Unknown, {}, "unknown format"};
    }
}

const char* decode_status_name(DecodeStatus status) noexcept {
    switch (status) {
        case DecodeStatus::Ok: return "OK";
        case DecodeStatus::UnknownFormat: return "UNKNOWN_FORMAT";
        case DecodeStatus::InvalidData: return "INVALID_DATA";
        case DecodeStatus::UnsupportedLayout: return "UNSUPPORTED_LAYOUT";
        case DecodeStatus::TooLarge: return "TOO_LARGE";
        default: return "UNKNOWN";
    }
}

}  // namespace dmcresource
