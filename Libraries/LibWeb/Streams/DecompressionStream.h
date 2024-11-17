#pragma once

#include "AK/MemoryStream.h"
#include "LibCompress/Deflate.h"
#include "LibCompress/Gzip.h"
#include "LibCompress/Zlib.h"
#include <LibWeb/Bindings/PlatformObject.h>
#include <LibWeb/Streams/GenericTransformStream.h>
#include <LibWeb/Streams/ReadableStream.h>
#include <LibWeb/Streams/WritableStream.h>
#include <AK/Variant.h>

namespace Web::Streams {

using namespace Compress;

// FIXME: Consider a shared base class for these decompressors
using DecompressorContext = Variant<NonnullOwnPtr<ZlibDecompressor>, NonnullOwnPtr<DeflateDecompressor>, NonnullOwnPtr<GzipDecompressor>>;

class DecompressionStream final : public Bindings::PlatformObject {
    WEB_PLATFORM_OBJECT(DecompressionStream, Bindings::PlatformObject);
    GC_DECLARE_ALLOCATOR(DecompressionStream);

public:
    virtual ~DecompressionStream() override;

    static WebIDL::ExceptionOr<GC::Ref<DecompressionStream>> construct_impl(JS::Realm&, Bindings::CompressionFormat format);

    GC::Ref<ReadableStream> readable() { return m_transform->readable(); }
    GC::Ref<WritableStream> writable() { return m_transform->writable(); }

private:
    explicit DecompressionStream(JS::Realm& realm, DecompressorContext context, NonnullOwnPtr<AllocatingMemoryStream> input_stream);
    virtual void initialize(JS::Realm&) override;
    virtual void visit_edges(Cell::Visitor&) override;
    Web::Bindings::CompressionFormat m_format;
    // GC::Ptr<GenericTransformStreamMixin> m_transform;
    GC::Ptr<TransformStream> m_transform;
    DecompressorContext m_context;
    NonnullOwnPtr<AllocatingMemoryStream> m_input_stream;
};

}
