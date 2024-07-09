#pragma once

#include <AK/String.h>

// FIXME: Generate this from the IDL file that just has an enum in it
namespace Web::Bindings {

enum class CompressionFormat {
    Deflate,
    DeflateRaw,
    Gzip,
};

inline String idl_enum_to_string(CompressionFormat value)
{
    switch (value) {
    case CompressionFormat::Deflate:
        return "deflate"_string;
    case CompressionFormat::DeflateRaw:
        return "deflate-raw"_string;
    case CompressionFormat::Gzip:
        return "gzip"_string;
    }
}

};
