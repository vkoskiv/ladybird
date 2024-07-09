#include <LibCompress/Deflate.h>
#include <LibCompress/Gzip.h>
#include <LibCompress/Zlib.h>
#include <LibJS/Forward.h>
#include <LibGC/Heap.h>
#include <LibJS/Runtime/ArrayBuffer.h>
#include <LibJS/Runtime/Realm.h>
#include <LibJS/Runtime/TypedArray.h>
#include <LibWeb/Bindings/CompressionStreamPrototype.h>
#include <LibWeb/Bindings/Intrinsics.h>
#include <LibWeb/Forward.h>
#include <LibWeb/Streams/AbstractOperations.h>
#include <LibWeb/Streams/CompressionStream.h>
#include <LibWeb/WebIDL/AbstractOperations.h>
#include <LibWeb/WebIDL/Buffers.h>
#include <LibWeb/Streams/CompressionFormat.h>

namespace Web::Streams {

GC_DEFINE_ALLOCATOR(CompressionStream);

// https://compression.spec.whatwg.org//#compressionstream
WebIDL::ExceptionOr<GC::Ref<CompressionStream>> CompressionStream::construct_impl(JS::Realm& realm, Bindings::CompressionFormat format)
{

    auto compression_stream = realm.create<CompressionStream>(realm);
    // 1. If format is unsupported in CompressionStream, then throw a TypeError.
    // Already handled by IDL layer

    // 2. Set this's format to format.
    compression_stream->m_format = format;

    // 3. Let transformAlgorithm be an algorithm which takes a chunk argument and runs the compress and enqueue a chunk algorithm with this and chunk.
    auto compress_algorithm = GC::create_function(realm.heap(), [&realm, compression_stream](JS::Value chunk) -> GC::Ref<WebIDL::Promise> {
        // https://compression.spec.whatwg.org//#compress-and-enqueue-a-chunk
        // 1. If chunk is not a BufferSource type, then throw a TypeError.
        // FIXME: Not sure if this is a mistake in the spec, but we can't actually throw a TypeError
        // from a transformAlgorithm at the moment, so I'll return a rejected promise with the type error instead for now.
        // if (!chunk.is_object() || !is<WebIDL::BufferSource>(chunk.as_object())) {
        //     return WebIDL::create_rejected_promise(realm, JS::TypeError::create(realm, "Chunk not a BufferSource"sv));
        // }
        // FIXME: Write a check, for BufferSource since one doesn't seem to exist.
        // if (!chunk.is_object()) {
        //     return WebIDL::create_rejected_promise(realm, JS::TypeError::create(realm, "Chunk not an object"sv));
        // }
        // if (!is<WebIDL::BufferSource>(chunk.as_object())) {
        //     dbgln("Chunk type is {}", chunk.as_object().class_name());
        //     return WebIDL::create_rejected_promise(realm, JS::TypeError::create(realm, "Chunk not a BufferSource"sv));
        // }

        // 2. Let buffer be the result of compressing chunk with cs's format and context.
        auto chunk_view = realm.create<WebIDL::ArrayBufferView>(chunk.as_object());
        auto bytebuffer = WebIDL::get_buffer_source_copy(*chunk_view->raw_object());
        ReadonlyBytes data = bytebuffer.release_value_but_fixme_should_propagate_errors().bytes();

        // FIXME: We very likely can't use compress_all() like this, and need to instead use the underlying Stream API of the different compressors
        ByteBuffer buffer;
        switch (compression_stream->m_format) {
        case Bindings::CompressionFormat::Deflate: {
            buffer = MUST(Compress::ZlibCompressor::compress_all(data, Compress::ZlibCompressionLevel::Default));
            break;
        }
        case Web::Bindings::CompressionFormat::DeflateRaw: {
            buffer = MUST(Compress::DeflateCompressor::compress_all(data, Compress::DeflateCompressor::CompressionLevel::GOOD));
            break;
        }
        case Web::Bindings::CompressionFormat::Gzip: {
            buffer = MUST(Compress::GzipCompressor::compress_all(data));
            break;
        }
        }

        // 3. If buffer is empty, return.
        if (buffer.is_empty())
            return WebIDL::create_resolved_promise(realm, JS::js_undefined());

        // 4. Split buffer into one or more non-empty pieces and convert them into Uint8Arrays.
        // TODO: It's a bit vague on how big the pieces should be, no? I'll just skip this for now.

        // 5. For each Uint8Array array, enqueue array in cs's transform.
        auto bytes_length = buffer.size();
        auto array_buffer = JS::ArrayBuffer::create(realm, move(buffer));
        auto uint8_array = JS::Uint8Array::create(realm, bytes_length, *array_buffer);
        MUST(Streams::transform_stream_default_controller_enqueue(*compression_stream->m_transform->controller(), uint8_array));
        return WebIDL::create_resolved_promise(realm, JS::js_undefined());
    });

    // 4. Let flushAlgorithm be an algorithm which takes no argument and runs the compress flush and enqueue algorithm with this.
    auto flush_algorithm = GC::create_function(realm.heap(), [&realm, compression_stream]() -> GC::Ref<WebIDL::Promise> {
        // 1. Let buffer be the result of compressing an empty input with cs's format and context, with the finish flag.
        ByteBuffer compressed;
        switch (compression_stream->m_format) {
        case Bindings::CompressionFormat::Deflate: {
            auto deflate = Compress::ZlibCompressor::compress_all(ReadonlyBytes {}, Compress::ZlibCompressionLevel::Default);
            compressed = deflate.release_value_but_fixme_should_propagate_errors();
            break;
        }
        case Bindings::CompressionFormat::DeflateRaw: {
            auto deflate_raw = Compress::DeflateCompressor::compress_all(ReadonlyBytes {}, Compress::DeflateCompressor::CompressionLevel::GOOD);
            compressed = deflate_raw.release_value_but_fixme_should_propagate_errors();
            break;
        }
        case Bindings::CompressionFormat::Gzip: {
            auto gzip = Compress::GzipCompressor::compress_all(ReadonlyBytes {});
            compressed = gzip.release_value_but_fixme_should_propagate_errors();
            break;
        }
        }

        // 2. If buffer is empty, return.
        if (compressed.is_empty())
            return WebIDL::create_resolved_promise(realm, JS::js_undefined());

        // 3. Split buffer into one or more non-empty pieces and convert them into Uint8Arrays.
        // TODO

        // 4. For each Uint8Array array, enqueue array in cs's transform.
        auto bytes_length = compressed.size();
        auto array_buffer = JS::ArrayBuffer::create(realm, move(compressed));
        auto uint8_array = JS::Uint8Array::create(realm, bytes_length, *array_buffer);
        MUST(Streams::transform_stream_default_controller_enqueue(*compression_stream->m_transform->controller(), uint8_array));
        return WebIDL::create_resolved_promise(realm, JS::js_undefined());
    });

    // 5. Set this's transform to a new TransformStream.
    auto transform_stream = realm.create<TransformStream>(realm);

    // 6. Set up this's transform with transformAlgorithm set to transformAlgorithm and flushAlgorithm set to flushAlgorithm.
    Streams::transform_stream_set_up(transform_stream, compress_algorithm, flush_algorithm);

    compression_stream->m_transform = transform_stream;

    return compression_stream;
}

CompressionStream::CompressionStream(JS::Realm& realm)
    : Bindings::PlatformObject(realm)
{
}

CompressionStream::~CompressionStream() = default;

void CompressionStream::initialize(JS::Realm& realm)
{
    Base::initialize(realm);
    WEB_SET_PROTOTYPE_FOR_INTERFACE(CompressionStream);
}

void CompressionStream::visit_edges(Cell::Visitor& visitor)
{
    Base::visit_edges(visitor);
    visitor.visit(m_transform);
}
}
