#include "AK/MemoryStream.h"
#include "LibWeb/Bindings/ExceptionOrUtils.h"
#include <LibCompress/Deflate.h>
#include <LibCompress/Gzip.h>
#include <LibCompress/Zlib.h>
#include <LibJS/Forward.h>
#include <LibGC/Heap.h>
#include <LibJS/Runtime/ArrayBuffer.h>
#include <LibJS/Runtime/Realm.h>
#include <LibJS/Runtime/TypedArray.h>
#include <LibWeb/Bindings/DecompressionStreamPrototype.h>
#include <LibWeb/Bindings/Intrinsics.h>
#include <LibWeb/Forward.h>
#include <LibWeb/Streams/AbstractOperations.h>
#include <LibWeb/Streams/DecompressionStream.h>
#include <LibWeb/WebIDL/AbstractOperations.h>
#include <LibWeb/WebIDL/Buffers.h>
#include <LibWeb/Streams/CompressionFormat.h>

namespace Web::Streams {

GC_DEFINE_ALLOCATOR(DecompressionStream);

static ErrorOr<DecompressorContext> decompressor_from_type(Bindings::CompressionFormat format, NonnullOwnPtr<AK::AllocatingMemoryStream> stream) {
    switch (format) {
    case Bindings::CompressionFormat::Deflate: {
        return TRY(ZlibDecompressor::create(move(stream)));
    }
    case Bindings::CompressionFormat::DeflateRaw: {
        auto bit_stream = make<LittleEndianInputBitStream>(move(stream));
        return TRY(DeflateDecompressor::construct(move(bit_stream)));
    }
    case Bindings::CompressionFormat::Gzip: {
        return make<GzipDecompressor>(move(stream));
    }
    }
    VERIFY_NOT_REACHED();
}

// https://compression.spec.whatwg.org//#decompressionstream
WebIDL::ExceptionOr<GC::Ref<DecompressionStream>> DecompressionStream::construct_impl(JS::Realm& realm, Bindings::CompressionFormat format)
{
    auto input_stream = make<AK::AllocatingMemoryStream>();
    auto context = MUST(decompressor_from_type(format, move(input_stream)));
    auto decompression_stream = realm.create<DecompressionStream>(realm, move(context));

    // 1. If format is unsupported in DecompressionStream, then throw a TypeError.
    // Already handled by IDL layer

    // 2. Set this's format to format.
    decompression_stream->m_format = format;

    // 3. Let transformAlgorithm be an algorithm which takes a chunk argument and runs the compress and enqueue a chunk algorithm with this and chunk.
    auto compress_algorithm = GC::create_function(realm.heap(), [&realm, decompression_stream](JS::Value chunk) -> GC::Ref<WebIDL::Promise> {
        // https://compression.spec.whatwg.org//#decompress-and-enqueue-a-chunk
        // 1. If chunk is not a BufferSource type, then throw a TypeError.
        // FIXME: Not sure if this is a mistake in the spec, but we can't actually throw a TypeError
        // from a transformAlgorithm at the moment, so I'll return a rejected promise with the type error instead for now.
        if (!chunk.is_object() || !is<WebIDL::BufferSource>(chunk.as_object())) {
            return WebIDL::create_rejected_promise(realm, JS::TypeError::create(realm, "Chunk not a BufferSource"sv));
        }

        // 2. Let buffer be the result of decompressing chunk with cs's format and context.
        auto chunk_view = realm.create<WebIDL::ArrayBufferView>(chunk.as_object());
        auto bytebuffer = MUST(WebIDL::get_buffer_source_copy(*chunk_view->raw_object()));
        ReadonlyBytes data = bytebuffer.bytes();

        size_t bytes = MUST(decompression_stream->m_context.visit(
            [&](auto& ctx) -> ErrorOr<size_t> {
                return ctx->write_some(data);
            }
        ));
        (void)bytes;
        dbgln("DecompressionStream wrote {} bytes", bytes);

        ByteBuffer buffer;
        Bytes read_bytes = MUST(decompression_stream->m_context.visit(
            [&](auto& ctx) -> ErrorOr<Bytes> {
                return ctx->read_some(buffer);
            }
        ));

        dbgln("DecompressionStream read {} bytes", read_bytes.size());

        // 3. If buffer is empty, return.
        if (read_bytes.size() == 0)
            return WebIDL::create_resolved_promise(realm, JS::js_undefined());

        // 4. Split buffer into one or more non-empty pieces and convert them into Uint8Arrays.
        // TODO: It's a bit vague on how big the pieces should be, no? I'll just skip this for now.

        // 5. For each Uint8Array array, enqueue array in cs's transform.
        auto array_buffer = JS::ArrayBuffer::create(realm, move(buffer));
        auto uint8_array = JS::Uint8Array::create(realm, read_bytes.size(), *array_buffer);
        MUST(Streams::transform_stream_default_controller_enqueue(*decompression_stream->m_transform->controller(), uint8_array));
        return WebIDL::create_resolved_promise(realm, JS::js_undefined());
    });

    // 4. Let flushAlgorithm be an algorithm which takes no argument and runs the decompress flush and enqueue algorithm with this.
    auto flush_algorithm = GC::create_function(realm.heap(), [&realm, decompression_stream]() -> GC::Ref<WebIDL::Promise> {

        // 1. Let buffer be the result of decompressing an empty input with cs's format and context, with the finish flag.
        decompression_stream->m_context.visit(
            [](auto& ctx) -> void {
                ctx->close();
            }
        );

        ErrorOr<ByteBuffer> buf_or_err = decompression_stream->m_context.visit(
            [&](auto& ctx) -> ErrorOr<ByteBuffer> {
                return ctx->read_until_eof();
            }
        );

        if (buf_or_err.is_error())
            return WebIDL::create_resolved_promise(realm, JS::js_undefined());
            // return WebIDL::create_rejected_promise(realm, JS::PrimitiveString::create(realm.vm(), "EOF not reached"sv));

        ByteBuffer buffer = buf_or_err.value();

        // 2. If the end of the compressed input has not been reached, then throw a TypeError.
        bool is_eof = decompression_stream->m_context.visit(
            [&](auto& ctx) -> bool {
                return ctx->is_eof();
            }
        );
        // FIXME: Why can I put a JS::TypeError into this thing in the above lambda, but not this one?
        if (!is_eof) return WebIDL::create_rejected_promise(realm, JS::PrimitiveString::create(realm.vm(), "End of compressed input not reached"sv));
            
        // 3. If buffer is empty, return.
        if (buffer.is_empty())
            return WebIDL::create_resolved_promise(realm, JS::js_undefined());

        // 4. Split buffer into one or more non-empty pieces and convert them into Uint8Arrays.
        // TODO

        // 5. For each Uint8Array array, enqueue array in cs's transform.
        auto bytes_length = buffer.size();
        auto array_buffer = JS::ArrayBuffer::create(realm, move(buffer));
        auto uint8_array = JS::Uint8Array::create(realm, bytes_length, *array_buffer);
        auto result = Streams::transform_stream_default_controller_enqueue(*decompression_stream->m_transform->controller(), uint8_array);
        if (result.is_error()) {
            auto throw_completion = Bindings::dom_exception_to_throw_completion(realm.vm(), result.exception());
            return WebIDL::create_rejected_promise(realm, *throw_completion.release_value());
        }
        return WebIDL::create_resolved_promise(realm, JS::js_undefined());
    });

    // 5. Set this's transform to a new TransformStream.
    auto transform_stream = realm.heap().allocate<TransformStream>(realm);

    // 6. Set up this's transform with transformAlgorithm set to transformAlgorithm and flushAlgorithm set to flushAlgorithm.
    Streams::transform_stream_set_up(transform_stream, compress_algorithm, flush_algorithm);

    decompression_stream->m_transform = transform_stream;

    return decompression_stream;
}

DecompressionStream::DecompressionStream(JS::Realm& realm, DecompressorContext context)
    : Bindings::PlatformObject(realm), m_context(move(context))
{
}

DecompressionStream::~DecompressionStream() = default;

void DecompressionStream::initialize(JS::Realm& realm)
{
    Base::initialize(realm);
    WEB_SET_PROTOTYPE_FOR_INTERFACE(DecompressionStream);
}

void DecompressionStream::visit_edges(Cell::Visitor& visitor)
{
    Base::visit_edges(visitor);
    visitor.visit(m_transform);
}
}

