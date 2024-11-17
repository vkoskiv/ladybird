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

static ErrorOr<CompressorContext> compressor_from_type(Bindings::CompressionFormat format, MaybeOwned<Stream> stream) {
    switch (format) {
    case Bindings::CompressionFormat::Deflate: {
        dbgln("creating ZlibCompressor");
        return TRY(ZlibCompressor::construct(MaybeOwned<Stream>(*stream)));
    }
    case Bindings::CompressionFormat::DeflateRaw: {
        dbgln("creating DeflateCompressor");
        auto bit_stream = make<LittleEndianInputBitStream>(move(stream));
        return TRY(DeflateCompressor::construct(move(bit_stream)));
    }
    case Bindings::CompressionFormat::Gzip: {
        dbgln("creating GzipCompressor");
        return make<GzipCompressor>(move(stream));
    }
    }
    VERIFY_NOT_REACHED();
}

// https://compression.spec.whatwg.org//#compressionstream
WebIDL::ExceptionOr<GC::Ref<CompressionStream>> CompressionStream::construct_impl(JS::Realm& realm, Bindings::CompressionFormat format)
{
    dbgln("-------construct_impl-------");

    auto output_stream = make<AK::AllocatingMemoryStream>();
    auto context = MUST(compressor_from_type(format, MaybeOwned<Stream>(*output_stream)));
    auto compression_stream = realm.create<CompressionStream>(realm, move(context), move(output_stream));
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
        auto bytebuffer = MUST(WebIDL::get_buffer_source_copy(*chunk_view->raw_object()));
        ReadonlyBytes data = bytebuffer.bytes();

        // TODO: Try compress_all again I guess. The spec is vague about it, but I guess each chunk is individually compressed after all?
        #if 0
        ErrorOr<ByteBuffer> compressed = compression_stream->m_context.visit(
            [&](auto& ctx) -> ErrorOr<ByteBuffer> {
                return ctx->compress_all(data);
            }
        );
        if (compressed.is_error()) {
            dbgln("goofed: {}", compressed.error());
            return WebIDL::create_resolved_promise(realm, JS::js_undefined());
        }
        ByteBuffer buffer = compressed.release_value();
        Bytes read_bytes = buffer.bytes();
        #else
        size_t written_bytes = MUST(compression_stream->m_context.visit(
            [&](auto& ctx) -> ErrorOr<size_t> {
                return ctx->write_some(data);
            }
        ));
        (void)written_bytes;
        dbgln("CompressionStream wrote {} bytes", written_bytes);
        // auto buffer = MUST(ByteBuffer::create_uninitialized(compression_stream->m_output_stream->used_buffer_size()));
        // MUST(compression_stream->m_output_stream->read_until_filled(buffer.bytes()));
        // Bytes read_bytes = buffer.bytes();
        // dbgln("CompressionStream read {} bytes", read_bytes.size());
        ByteBuffer buffer;
        ErrorOr<Bytes> read_bytes_or_error = compression_stream->m_output_stream->read_some(buffer); // TODO: or do we want read_until_eof?
        if (read_bytes_or_error.is_error()) {
            dbgln("goofed: {}", read_bytes_or_error.error());
            return WebIDL::create_resolved_promise(realm, JS::js_undefined());
        }
        Bytes read_bytes = read_bytes_or_error.release_value();
        dbgln("CompressionStream read {} bytes", read_bytes.size());
        #endif
        // 3. If buffer is empty, return.
        if (read_bytes.is_empty())
            return WebIDL::create_resolved_promise(realm, JS::js_undefined());

        // 4. Split buffer into one or more non-empty pieces and convert them into Uint8Arrays.
        // TODO: It's a bit vague on how big the pieces should be, no? I'll just skip this for now.

        // 5. For each Uint8Array array, enqueue array in cs's transform.
        auto array_buffer = JS::ArrayBuffer::create(realm, move(buffer));
        auto uint8_array = JS::Uint8Array::create(realm, read_bytes.size(), *array_buffer);
        MUST(Streams::transform_stream_default_controller_enqueue(*compression_stream->m_transform->controller(), uint8_array));
        return WebIDL::create_resolved_promise(realm, JS::js_undefined());
    });

    // 4. Let flushAlgorithm be an algorithm which takes no argument and runs the compress flush and enqueue algorithm with this.
    auto flush_algorithm = GC::create_function(realm.heap(), [&realm, compression_stream]() -> GC::Ref<WebIDL::Promise> {
        // 1. Let buffer be the result of compressing an empty input with cs's format and context, with the finish flag.
        compression_stream->m_context.visit(
            [](auto& ctx) -> void {
                ctx->close();
            }
        );
        ErrorOr<ByteBuffer> buf_or_err = compression_stream->m_output_stream->read_until_eof();
        if (buf_or_err.is_error()) {
            dbgln("flush goofed: {}", buf_or_err.error());
            return WebIDL::create_resolved_promise(realm, JS::js_undefined());
        }

        ByteBuffer buffer = buf_or_err.value();
        dbgln("flush_algorigthm read {} bytes", buffer.size());
        
        // 2. If buffer is empty, return.
        if (buffer.is_empty())
            return WebIDL::create_resolved_promise(realm, JS::js_undefined());

        // 3. Split buffer into one or more non-empty pieces and convert them into Uint8Arrays.
        // TODO

        // 4. For each Uint8Array array, enqueue array in cs's transform.
        auto bytes_length = buffer.size();
        auto array_buffer = JS::ArrayBuffer::create(realm, move(buffer));
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

CompressionStream::CompressionStream(JS::Realm& realm, CompressorContext context, NonnullOwnPtr<AllocatingMemoryStream> output_stream)
    : Bindings::PlatformObject(realm), m_context(move(context)), m_output_stream(move(output_stream))
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
