#pragma once

#include <LibWeb/Bindings/PlatformObject.h>
#include <LibWeb/Streams/GenericTransformStream.h>
#include <LibWeb/Streams/ReadableStream.h>
#include <LibWeb/Streams/WritableStream.h>

namespace Web::Streams {

class CompressionStream final : public Bindings::PlatformObject {
    WEB_PLATFORM_OBJECT(CompressionStream, Bindings::PlatformObject);
    GC_DECLARE_ALLOCATOR(CompressionStream);

public:
    virtual ~CompressionStream() override;

    static WebIDL::ExceptionOr<GC::Ref<CompressionStream>> construct_impl(JS::Realm&, Bindings::CompressionFormat format);

    GC::Ref<ReadableStream> readable() { return m_transform->readable(); }
    GC::Ref<WritableStream> writable() { return m_transform->writable(); }

private:
    explicit CompressionStream(JS::Realm& realm);
    virtual void initialize(JS::Realm&) override;
    virtual void visit_edges(Cell::Visitor&) override;
    Web::Bindings::CompressionFormat m_format;
    // JS::GCPtr<GenericTransformStreamMixin> m_transform;
    GC::Ptr<TransformStream> m_transform;
};

}
