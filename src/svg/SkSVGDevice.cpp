/*
 * Copyright 2015 Google Inc.
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "SkSVGDevice.h"

#include "SkAnnotationKeys.h"
#include "SkBase64.h"
#include "SkBitmap.h"
#include "SkBlendMode.h"
#include "SkChecksum.h"
#include "SkClipOpPriv.h"
#include "SkClipStack.h"
#include "SkColorFilter.h"
#include "SkData.h"
#include "SkDraw.h"
#include "SkImage.h"
#include "SkImageEncoder.h"
#include "SkJpegCodec.h"
#include "SkPaint.h"
#include "SkPaintPriv.h"
#include "SkParsePath.h"
#include "SkPngCodec.h"
#include "SkShader.h"
#include "SkStream.h"
#include "SkTHash.h"
#include "SkTo.h"
#include "SkTypeface.h"
#include "SkUtils.h"
#include "SkXMLWriter.h"

namespace {

static SkString svg_color(SkColor color) {
    return SkStringPrintf("rgb(%u,%u,%u)",
                          SkColorGetR(color),
                          SkColorGetG(color),
                          SkColorGetB(color));
}

static SkScalar svg_opacity(SkColor color) {
    return SkIntToScalar(SkColorGetA(color)) / SK_AlphaOPAQUE;
}

// Keep in sync with SkPaint::Cap
static const char* cap_map[]  = {
    nullptr,    // kButt_Cap (default)
    "round", // kRound_Cap
    "square" // kSquare_Cap
};
static_assert(SK_ARRAY_COUNT(cap_map) == SkPaint::kCapCount, "missing_cap_map_entry");

static const char* svg_cap(SkPaint::Cap cap) {
    SkASSERT(cap < SK_ARRAY_COUNT(cap_map));
    return cap_map[cap];
}

// Keep in sync with SkPaint::Join
static const char* join_map[] = {
    nullptr,    // kMiter_Join (default)
    "round", // kRound_Join
    "bevel"  // kBevel_Join
};
static_assert(SK_ARRAY_COUNT(join_map) == SkPaint::kJoinCount, "missing_join_map_entry");

static const char* svg_join(SkPaint::Join join) {
    SkASSERT(join < SK_ARRAY_COUNT(join_map));
    return join_map[join];
}

// Keep in sync with SkPaint::Align
static const char* text_align_map[] = {
    nullptr,     // kLeft_Align (default)
    "middle", // kCenter_Align
    "end"     // kRight_Align
};
static_assert(SK_ARRAY_COUNT(text_align_map) == SkPaint::kAlignCount,
              "missing_text_align_map_entry");
static const char* svg_text_align(SkPaint::Align align) {
    SkASSERT(align < SK_ARRAY_COUNT(text_align_map));
    return text_align_map[align];
}

static SkString svg_transform(const SkMatrix& t) {
    SkASSERT(!t.isIdentity());

    SkString tstr;
    switch (t.getType()) {
    case SkMatrix::kPerspective_Mask:
        // TODO: handle perspective matrices?
        break;
    case SkMatrix::kTranslate_Mask:
        tstr.printf("translate(%g %g)", t.getTranslateX(), t.getTranslateY());
        break;
    case SkMatrix::kScale_Mask:
        tstr.printf("scale(%g %g)", t.getScaleX(), t.getScaleY());
        break;
    default:
        // http://www.w3.org/TR/SVG/coords.html#TransformMatrixDefined
        //    | a c e |
        //    | b d f |
        //    | 0 0 1 |
        tstr.printf("matrix(%g %g %g %g %g %g)",
                    t.getScaleX(),     t.getSkewY(),
                    t.getSkewX(),      t.getScaleY(),
                    t.getTranslateX(), t.getTranslateY());
        break;
    }

    return tstr;
}

struct Resources {
    Resources(const SkPaint& paint)
        : fPaintServer(svg_color(paint.getColor())) {}

    SkString fPaintServer;
    SkString fClip;
    SkString fColorFilter;
};

static SkTypeface::Encoding to_encoding(SkPaint::TextEncoding e) {
    static_assert((int)SkTypeface::kUTF8_Encoding  == (int)SkPaint::kUTF8_TextEncoding,  "");
    static_assert((int)SkTypeface::kUTF16_Encoding == (int)SkPaint::kUTF16_TextEncoding, "");
    static_assert((int)SkTypeface::kUTF32_Encoding == (int)SkPaint::kUTF32_TextEncoding, "");
    return (SkTypeface::Encoding)e;
}

class SVGTextBuilder : SkNoncopyable {
public:
    SVGTextBuilder(const void* text, size_t byteLen, const SkPaint& paint, const SkPoint& offset,
                   unsigned scalarsPerPos, const SkScalar pos[] = nullptr)
        : fOffset(offset)
        , fScalarsPerPos(scalarsPerPos)
        , fPos(pos)
        , fLastCharWasWhitespace(true) // start off in whitespace mode to strip all leading space
    {
        SkASSERT(scalarsPerPos <= 2);
        SkASSERT(scalarsPerPos == 0 || SkToBool(pos));

        SkPaint::TextEncoding encoding = paint.getTextEncoding();
        switch(encoding) {
            case SkPaint::kGlyphID_TextEncoding: {
                int count = paint.countText(text, byteLen);
                SkASSERT(count * sizeof(uint16_t) == byteLen);
                SkAutoSTArray<64, SkUnichar> unichars(count);
                paint.glyphsToUnichars((const uint16_t*)text, count, unichars.get());
                for (int i = 0; i < count; ++i) {
                    this->appendUnichar(unichars[i]);
                }
                break;
            }
            case SkPaint::kUTF8_TextEncoding:
            case SkPaint::kUTF16_TextEncoding:
            case SkPaint::kUTF32_TextEncoding: {
                const void* stop = (const char*)text + byteLen;
                while (text < stop) {
                    this->appendUnichar(SkUTFN_Next(to_encoding(encoding), &text, stop));
                }
                break;
            }
            default:
                SK_ABORT("unknown text encoding");
        }

        if (scalarsPerPos < 2) {
            SkASSERT(fPosY.isEmpty());
            fPosY.appendScalar(offset.y()); // DrawText or DrawPosTextH (fixed Y).
        }

        if (scalarsPerPos < 1) {
            SkASSERT(fPosX.isEmpty());
            fPosX.appendScalar(offset.x()); // DrawText (X also fixed).
        }
    }

    const SkString& text() const { return fText; }
    const SkString& posX() const { return fPosX; }
    const SkString& posY() const { return fPosY; }

private:
    void appendUnichar(SkUnichar c) {
        bool discardPos = false;
        bool isWhitespace = false;

        switch(c) {
        case ' ':
        case '\t':
            // consolidate whitespace to match SVG's xml:space=default munging
            // (http://www.w3.org/TR/SVG/text.html#WhiteSpace)
            if (fLastCharWasWhitespace) {
                discardPos = true;
            } else {
                fText.appendUnichar(c);
            }
            isWhitespace = true;
            break;
        case '\0':
            // SkPaint::glyphsToUnichars() returns \0 for inconvertible glyphs, but these
            // are not legal XML characters (http://www.w3.org/TR/REC-xml/#charsets)
            discardPos = true;
            isWhitespace = fLastCharWasWhitespace; // preserve whitespace consolidation
            break;
        case '&':
            fText.append("&amp;");
            break;
        case '"':
            fText.append("&quot;");
            break;
        case '\'':
            fText.append("&apos;");
            break;
        case '<':
            fText.append("&lt;");
            break;
        case '>':
            fText.append("&gt;");
            break;
        default:
            fText.appendUnichar(c);
            break;
        }

        this->advancePos(discardPos);
        fLastCharWasWhitespace = isWhitespace;
    }

    void advancePos(bool discard) {
        if (!discard && fScalarsPerPos > 0) {
            fPosX.appendf("%.8g, ", fOffset.x() + fPos[0]);
            if (fScalarsPerPos > 1) {
                SkASSERT(fScalarsPerPos == 2);
                fPosY.appendf("%.8g, ", fOffset.y() + fPos[1]);
            }
        }
        fPos += fScalarsPerPos;
    }

    const SkPoint&  fOffset;
    const unsigned  fScalarsPerPos;
    const SkScalar* fPos;

    SkString fText, fPosX, fPosY;
    bool     fLastCharWasWhitespace;
};

// Determine if the paint requires us to reset the viewport.
// Currently, we do this whenever the paint shader calls
// for a repeating image.
bool RequiresViewportReset(const SkPaint& paint) {
  SkShader* shader = paint.getShader();
  if (!shader)
    return false;

  SkShader::TileMode xy[2];
  SkImage* image = shader->isAImage(nullptr, xy);

  if (!image)
    return false;

  for (int i = 0; i < 2; i++) {
    if (xy[i] == SkShader::kRepeat_TileMode)
      return true;
  }
  return false;
}

}  // namespace

// For now all this does is serve unique serial IDs, but it will eventually evolve to track
// and deduplicate resources.
class SkSVGDevice::ResourceBucket : ::SkNoncopyable {
public:
    ResourceBucket()
            : fGradientCount(0)
            , fClipCount(0)
            , fPathCount(0)
            , fImageCount(0)
            , fPatternCount(0)
            , fColorFilterCount(0) {}

    SkString addLinearGradient() {
        return SkStringPrintf("gradient_%d", fGradientCount++);
    }

    SkString addClip() {
        return SkStringPrintf("clip_%d", fClipCount++);
    }

    SkString addPath() {
        return SkStringPrintf("path_%d", fPathCount++);
    }

    SkString addImage() {
        return SkStringPrintf("img_%d", fImageCount++);
    }

    SkString addColorFilter() { return SkStringPrintf("cfilter_%d", fColorFilterCount++); }

    SkString addPattern() {
      return SkStringPrintf("pattern_%d", fPatternCount++);
    }

private:
    uint32_t fGradientCount;
    uint32_t fClipCount;
    uint32_t fPathCount;
    uint32_t fImageCount;
    uint32_t fPatternCount;
    uint32_t fColorFilterCount;
};

struct SkSVGDevice::MxCp {
    const SkMatrix* fMatrix;
    const SkClipStack*  fClipStack;

    MxCp(const SkMatrix* mx, const SkClipStack* cs) : fMatrix(mx), fClipStack(cs) {}
    MxCp(SkSVGDevice* device) : fMatrix(&device->ctm()), fClipStack(&device->cs()) {}
};

class SkSVGDevice::AutoElement : ::SkNoncopyable {
public:
    AutoElement(const char name[], SkXMLWriter* writer)
        : fWriter(writer)
        , fResourceBucket(nullptr) {
        fWriter->startElement(name);
    }

    AutoElement(const char name[], SkXMLWriter* writer, ResourceBucket* bucket,
                const MxCp& mc, const SkPaint& paint)
        : fWriter(writer)
        , fResourceBucket(bucket) {

        Resources res = this->addResources(mc, paint);

        if (!res.fClip.isEmpty()) {
            // The clip is in device space. Apply it via a <g> wrapper to avoid local transform
            // interference.
            fClipGroup.reset(new AutoElement("g", fWriter));
            fClipGroup->addAttribute("clip-path",res.fClip);
        }

        fWriter->startElement(name);

        this->addPaint(paint, res);

        if (!mc.fMatrix->isIdentity()) {
            this->addAttribute("transform", svg_transform(*mc.fMatrix));
        }
    }

    ~AutoElement() {
        fWriter->endElement();
    }

    void addAttribute(const char name[], const char val[]) {
        fWriter->addAttribute(name, val);
    }

    void addAttribute(const char name[], const SkString& val) {
        fWriter->addAttribute(name, val.c_str());
    }

    void addAttribute(const char name[], int32_t val) {
        fWriter->addS32Attribute(name, val);
    }

    void addAttribute(const char name[], SkScalar val) {
        fWriter->addScalarAttribute(name, val);
    }

    void addText(const SkString& text) {
        fWriter->addText(text.c_str(), text.size());
    }

    void addRectAttributes(const SkRect&);
    void addPathAttributes(const SkPath&);
    void addTextAttributes(const SkPaint&);

private:
    Resources addResources(const MxCp&, const SkPaint& paint);
    void addClipResources(const MxCp&, Resources* resources);
    void addShaderResources(const SkPaint& paint, Resources* resources);
    void addGradientShaderResources(const SkShader* shader, const SkPaint& paint,
                                    Resources* resources);
    void addColorFilterResources(const SkColorFilter& cf, Resources* resources);
    void addImageShaderResources(const SkShader* shader, const SkPaint& paint,
                                 Resources* resources);

    void addPatternDef(const SkBitmap& bm);

    void addPaint(const SkPaint& paint, const Resources& resources);


    SkString addLinearGradientDef(const SkShader::GradientInfo& info, const SkShader* shader);

    SkXMLWriter*               fWriter;
    ResourceBucket*            fResourceBucket;
    std::unique_ptr<AutoElement> fClipGroup;
};

void SkSVGDevice::AutoElement::addPaint(const SkPaint& paint, const Resources& resources) {
    SkPaint::Style style = paint.getStyle();
    if (style == SkPaint::kFill_Style || style == SkPaint::kStrokeAndFill_Style) {
        this->addAttribute("fill", resources.fPaintServer);

        if (SK_AlphaOPAQUE != SkColorGetA(paint.getColor())) {
            this->addAttribute("fill-opacity", svg_opacity(paint.getColor()));
        }
    } else {
        SkASSERT(style == SkPaint::kStroke_Style);
        this->addAttribute("fill", "none");
    }

    if (!resources.fColorFilter.isEmpty()) {
        this->addAttribute("filter", resources.fColorFilter.c_str());
    }

    if (style == SkPaint::kStroke_Style || style == SkPaint::kStrokeAndFill_Style) {
        this->addAttribute("stroke", resources.fPaintServer);

        SkScalar strokeWidth = paint.getStrokeWidth();
        if (strokeWidth == 0) {
            // Hairline stroke
            strokeWidth = 1;
            this->addAttribute("vector-effect", "non-scaling-stroke");
        }
        this->addAttribute("stroke-width", strokeWidth);

        if (const char* cap = svg_cap(paint.getStrokeCap())) {
            this->addAttribute("stroke-linecap", cap);
        }

        if (const char* join = svg_join(paint.getStrokeJoin())) {
            this->addAttribute("stroke-linejoin", join);
        }

        if (paint.getStrokeJoin() == SkPaint::kMiter_Join) {
            this->addAttribute("stroke-miterlimit", paint.getStrokeMiter());
        }

        if (SK_AlphaOPAQUE != SkColorGetA(paint.getColor())) {
            this->addAttribute("stroke-opacity", svg_opacity(paint.getColor()));
        }
    } else {
        SkASSERT(style == SkPaint::kFill_Style);
        this->addAttribute("stroke", "none");
    }
}

Resources SkSVGDevice::AutoElement::addResources(const MxCp& mc, const SkPaint& paint) {
    Resources resources(paint);

    // FIXME: this is a weak heuristic and we end up with LOTS of redundant clips.
    bool hasClip   = !mc.fClipStack->isWideOpen();
    bool hasShader = SkToBool(paint.getShader());

    if (hasClip || hasShader) {
        AutoElement defs("defs", fWriter);

        if (hasClip) {
            this->addClipResources(mc, &resources);
        }

        if (hasShader) {
            this->addShaderResources(paint, &resources);
        }
    }

    if (const SkColorFilter* cf = paint.getColorFilter()) {
        // TODO: Implement skia color filters for blend modes other than SrcIn
        SkBlendMode mode;
        if (cf->asColorMode(nullptr, &mode) && mode == SkBlendMode::kSrcIn) {
            this->addColorFilterResources(*cf, &resources);
        }
    }
    return resources;
}

void SkSVGDevice::AutoElement::addGradientShaderResources(const SkShader* shader,
                                                          const SkPaint& paint,
                                                          Resources* resources) {
    SkShader::GradientInfo grInfo;
    grInfo.fColorCount = 0;
    if (SkShader::kLinear_GradientType != shader->asAGradient(&grInfo)) {
        // TODO: non-linear gradient support
        return;
    }

    SkAutoSTArray<16, SkColor>  grColors(grInfo.fColorCount);
    SkAutoSTArray<16, SkScalar> grOffsets(grInfo.fColorCount);
    grInfo.fColors = grColors.get();
    grInfo.fColorOffsets = grOffsets.get();

    // One more call to get the actual colors/offsets.
    shader->asAGradient(&grInfo);
    SkASSERT(grInfo.fColorCount <= grColors.count());
    SkASSERT(grInfo.fColorCount <= grOffsets.count());

    resources->fPaintServer.printf("url(#%s)", addLinearGradientDef(grInfo, shader).c_str());
}

void SkSVGDevice::AutoElement::addColorFilterResources(const SkColorFilter& cf,
                                                       Resources* resources) {
    SkString colorfilterID = fResourceBucket->addColorFilter();
    {
        AutoElement filterElement("filter", fWriter);
        filterElement.addAttribute("id", colorfilterID);
        filterElement.addAttribute("x", "0%");
        filterElement.addAttribute("y", "0%");
        filterElement.addAttribute("width", "100%");
        filterElement.addAttribute("height", "100%");

        SkColor filterColor;
        SkBlendMode mode;
        bool asColorMode = cf.asColorMode(&filterColor, &mode);
        SkAssertResult(asColorMode);
        SkASSERT(mode == SkBlendMode::kSrcIn);

        {
            // first flood with filter color
            AutoElement floodElement("feFlood", fWriter);
            floodElement.addAttribute("flood-color", svg_color(filterColor));
            floodElement.addAttribute("flood-opacity", svg_opacity(filterColor));
            floodElement.addAttribute("result", "flood");
        }

        {
            // apply the transform to filter color
            AutoElement compositeElement("feComposite", fWriter);
            compositeElement.addAttribute("in", "flood");
            compositeElement.addAttribute("operator", "in");
        }
    }
    resources->fColorFilter.printf("url(#%s)", colorfilterID.c_str());
}

// Returns data uri from bytes.
// it will use any cached data if available, otherwise will
// encode as png.
sk_sp<SkData> AsDataUri(SkImage* image) {
    sk_sp<SkData> imageData = image->encodeToData();
    if (!imageData) {
        return nullptr;
    }

    const char* src = (char*)imageData->data();
    const char* selectedPrefix = nullptr;
    size_t selectedPrefixLength = 0;

    const static char pngDataPrefix[] = "data:image/png;base64,";
    const static char jpgDataPrefix[] = "data:image/jpeg;base64,";

    if (SkJpegCodec::IsJpeg(src, imageData->size())) {
        selectedPrefix = jpgDataPrefix;
        selectedPrefixLength = sizeof(jpgDataPrefix);
    } else {
      if (!SkPngCodec::IsPng(src, imageData->size())) {
        imageData = image->encodeToData(SkEncodedImageFormat::kPNG, 100);
      }
      selectedPrefix = pngDataPrefix;
      selectedPrefixLength = sizeof(pngDataPrefix);
    }

    size_t b64Size = SkBase64::Encode(imageData->data(), imageData->size(), nullptr);
    sk_sp<SkData> dataUri = SkData::MakeUninitialized(selectedPrefixLength + b64Size);
    char* dest = (char*)dataUri->writable_data();
    memcpy(dest, selectedPrefix, selectedPrefixLength);
    SkBase64::Encode(imageData->data(), imageData->size(), dest + selectedPrefixLength - 1);
    dest[dataUri->size() - 1] = 0;
    return dataUri;
}

void SkSVGDevice::AutoElement::addImageShaderResources(const SkShader* shader, const SkPaint& paint,
                                                       Resources* resources) {
    SkMatrix outMatrix;

    SkShader::TileMode xy[2];
    SkImage* image = shader->isAImage(&outMatrix, xy);
    SkASSERT(image);

    SkString patternDims[2];  // width, height

    sk_sp<SkData> dataUri = AsDataUri(image);
    if (!dataUri) {
        return;
    }
    SkIRect imageSize = image->bounds();
    for (int i = 0; i < 2; i++) {
        int imageDimension = i == 0 ? imageSize.width() : imageSize.height();
        switch (xy[i]) {
            case SkShader::kRepeat_TileMode:
                patternDims[i].appendScalar(imageDimension);
            break;
            default:
                // TODO: other tile modes?
                patternDims[i] = "100%";
        }
    }

    SkString patternID = fResourceBucket->addPattern();
    {
        AutoElement pattern("pattern", fWriter);
        pattern.addAttribute("id", patternID);
        pattern.addAttribute("patternUnits", "userSpaceOnUse");
        pattern.addAttribute("patternContentUnits", "userSpaceOnUse");
        pattern.addAttribute("width", patternDims[0]);
        pattern.addAttribute("height", patternDims[1]);
        pattern.addAttribute("x", 0);
        pattern.addAttribute("y", 0);

        {
            SkString imageID = fResourceBucket->addImage();
            AutoElement imageTag("image", fWriter);
            imageTag.addAttribute("id", imageID);
            imageTag.addAttribute("x", 0);
            imageTag.addAttribute("y", 0);
            imageTag.addAttribute("width", image->width());
            imageTag.addAttribute("height", image->height());
            imageTag.addAttribute("xlink:href", static_cast<const char*>(dataUri->data()));
        }
    }
    resources->fPaintServer.printf("url(#%s)", patternID.c_str());
}

void SkSVGDevice::AutoElement::addShaderResources(const SkPaint& paint, Resources* resources) {
    const SkShader* shader = paint.getShader();
    SkASSERT(shader);

    if (shader->asAGradient(nullptr) != SkShader::kNone_GradientType) {
        this->addGradientShaderResources(shader, paint, resources);
    } else if (shader->isAImage()) {
        this->addImageShaderResources(shader, paint, resources);
    }
    // TODO: other shader types?
}

void SkSVGDevice::AutoElement::addClipResources(const MxCp& mc, Resources* resources) {
    SkASSERT(!mc.fClipStack->isWideOpen());

    SkPath clipPath;
    (void) mc.fClipStack->asPath(&clipPath);

    SkString clipID = fResourceBucket->addClip();
    const char* clipRule = clipPath.getFillType() == SkPath::kEvenOdd_FillType ?
                           "evenodd" : "nonzero";
    {
        // clipPath is in device space, but since we're only pushing transform attributes
        // to the leaf nodes, so are all our elements => SVG userSpaceOnUse == device space.
        AutoElement clipPathElement("clipPath", fWriter);
        clipPathElement.addAttribute("id", clipID);

        SkRect clipRect = SkRect::MakeEmpty();
        if (clipPath.isEmpty() || clipPath.isRect(&clipRect)) {
            AutoElement rectElement("rect", fWriter);
            rectElement.addRectAttributes(clipRect);
            rectElement.addAttribute("clip-rule", clipRule);
        } else {
            AutoElement pathElement("path", fWriter);
            pathElement.addPathAttributes(clipPath);
            pathElement.addAttribute("clip-rule", clipRule);
        }
    }

    resources->fClip.printf("url(#%s)", clipID.c_str());
}

SkString SkSVGDevice::AutoElement::addLinearGradientDef(const SkShader::GradientInfo& info,
                                                        const SkShader* shader) {
    SkASSERT(fResourceBucket);
    SkString id = fResourceBucket->addLinearGradient();

    {
        AutoElement gradient("linearGradient", fWriter);

        gradient.addAttribute("id", id);
        gradient.addAttribute("gradientUnits", "userSpaceOnUse");
        gradient.addAttribute("x1", info.fPoint[0].x());
        gradient.addAttribute("y1", info.fPoint[0].y());
        gradient.addAttribute("x2", info.fPoint[1].x());
        gradient.addAttribute("y2", info.fPoint[1].y());

        if (!shader->getLocalMatrix().isIdentity()) {
            this->addAttribute("gradientTransform", svg_transform(shader->getLocalMatrix()));
        }

        SkASSERT(info.fColorCount >= 2);
        for (int i = 0; i < info.fColorCount; ++i) {
            SkColor color = info.fColors[i];
            SkString colorStr(svg_color(color));

            {
                AutoElement stop("stop", fWriter);
                stop.addAttribute("offset", info.fColorOffsets[i]);
                stop.addAttribute("stop-color", colorStr.c_str());

                if (SK_AlphaOPAQUE != SkColorGetA(color)) {
                    stop.addAttribute("stop-opacity", svg_opacity(color));
                }
            }
        }
    }

    return id;
}

void SkSVGDevice::AutoElement::addRectAttributes(const SkRect& rect) {
    // x, y default to 0
    if (rect.x() != 0) {
        this->addAttribute("x", rect.x());
    }
    if (rect.y() != 0) {
        this->addAttribute("y", rect.y());
    }

    this->addAttribute("width", rect.width());
    this->addAttribute("height", rect.height());
}

void SkSVGDevice::AutoElement::addPathAttributes(const SkPath& path) {
    SkString pathData;
    SkParsePath::ToSVGString(path, &pathData);
    this->addAttribute("d", pathData);
}

void SkSVGDevice::AutoElement::addTextAttributes(const SkPaint& paint) {
    this->addAttribute("font-size", paint.getTextSize());

    if (const char* textAlign = svg_text_align(paint.getTextAlign())) {
        this->addAttribute("text-anchor", textAlign);
    }

    SkString familyName;
    SkTHashSet<SkString> familySet;
    sk_sp<SkTypeface> tface = SkPaintPriv::RefTypefaceOrDefault(paint);

    SkASSERT(tface);
    SkFontStyle style = tface->fontStyle();
    if (style.slant() == SkFontStyle::kItalic_Slant) {
        this->addAttribute("font-style", "italic");
    } else if (style.slant() == SkFontStyle::kOblique_Slant) {
        this->addAttribute("font-style", "oblique");
    }
    int weightIndex = (SkTPin(style.weight(), 100, 900) - 50) / 100;
    if (weightIndex != 3) {
        static constexpr const char* weights[] = {
            "100", "200", "300", "normal", "400", "500", "600", "bold", "800", "900"
        };
        this->addAttribute("font-weight", weights[weightIndex]);
    }
    int stretchIndex = style.width() - 1;
    if (stretchIndex != 4) {
        static constexpr const char* stretches[] = {
            "ultra-condensed", "extra-condensed", "condensed", "semi-condensed",
            "normal",
            "semi-expanded", "expanded", "extra-expanded", "ultra-expanded"
        };
        this->addAttribute("font-stretch", stretches[stretchIndex]);
    }

    sk_sp<SkTypeface::LocalizedStrings> familyNameIter(tface->createFamilyNameIterator());
    SkTypeface::LocalizedString familyString;
    if (familyNameIter) {
        while (familyNameIter->next(&familyString)) {
            if (familySet.contains(familyString.fString)) {
                continue;
            }
            familySet.add(familyString.fString);
            familyName.appendf((familyName.isEmpty() ? "%s" : ", %s"), familyString.fString.c_str());
        }
    }
    if (!familyName.isEmpty()) {
        this->addAttribute("font-family", familyName);
    }
}

SkBaseDevice* SkSVGDevice::Create(const SkISize& size, SkXMLWriter* writer) {
    if (!writer) {
        return nullptr;
    }

    return new SkSVGDevice(size, writer);
}

SkSVGDevice::SkSVGDevice(const SkISize& size, SkXMLWriter* writer)
    : INHERITED(SkImageInfo::MakeUnknown(size.fWidth, size.fHeight),
                SkSurfaceProps(0, kUnknown_SkPixelGeometry))
    , fWriter(writer)
    , fResourceBucket(new ResourceBucket)
{
    SkASSERT(writer);

    fWriter->writeHeader();

    // The root <svg> tag gets closed by the destructor.
    fRootElement.reset(new AutoElement("svg", fWriter));

    fRootElement->addAttribute("xmlns", "http://www.w3.org/2000/svg");
    fRootElement->addAttribute("xmlns:xlink", "http://www.w3.org/1999/xlink");
    fRootElement->addAttribute("width", size.width());
    fRootElement->addAttribute("height", size.height());
}

SkSVGDevice::~SkSVGDevice() {
}

void SkSVGDevice::drawPaint(const SkPaint& paint) {
    AutoElement rect("rect", fWriter, fResourceBucket.get(), MxCp(this), paint);
    rect.addRectAttributes(SkRect::MakeWH(SkIntToScalar(this->width()),
                                          SkIntToScalar(this->height())));
}

void SkSVGDevice::drawAnnotation(const SkRect& rect, const char key[], SkData* value) {
    if (!value) {
        return;
    }

    if (!strcmp(SkAnnotationKeys::URL_Key(), key) ||
        !strcmp(SkAnnotationKeys::Link_Named_Dest_Key(), key)) {
        this->cs().save();
        this->cs().clipRect(rect, this->ctm(), kIntersect_SkClipOp, true);
        SkRect transformedRect = this->cs().bounds(this->getGlobalBounds());
        this->cs().restore();
        if (transformedRect.isEmpty()) {
            return;
        }

        SkString url(static_cast<const char*>(value->data()), value->size() - 1);
        AutoElement a("a", fWriter);
        a.addAttribute("xlink:href", url.c_str());
        {
            AutoElement r("rect", fWriter);
            r.addAttribute("fill-opacity", "0.0");
            r.addRectAttributes(transformedRect);
        }
    }
}

void SkSVGDevice::drawPoints(SkCanvas::PointMode mode, size_t count,
                             const SkPoint pts[], const SkPaint& paint) {
    SkPath path;

    switch (mode) {
            // todo
        case SkCanvas::kPoints_PointMode:
            // TODO?
            break;
        case SkCanvas::kLines_PointMode:
            count -= 1;
            for (size_t i = 0; i < count; i += 2) {
                path.rewind();
                path.moveTo(pts[i]);
                path.lineTo(pts[i+1]);
                AutoElement elem("path", fWriter, fResourceBucket.get(), MxCp(this), paint);
                elem.addPathAttributes(path);
            }
            break;
        case SkCanvas::kPolygon_PointMode:
            if (count > 1) {
                path.addPoly(pts, SkToInt(count), false);
                path.moveTo(pts[0]);
                AutoElement elem("path", fWriter, fResourceBucket.get(), MxCp(this), paint);
                elem.addPathAttributes(path);
            }
            break;
    }
}

void SkSVGDevice::drawRect(const SkRect& r, const SkPaint& paint) {
    std::unique_ptr<AutoElement> svg;
    if (RequiresViewportReset(paint)) {
      svg.reset(new AutoElement("svg", fWriter, fResourceBucket.get(), MxCp(this), paint));
      svg->addRectAttributes(r);
    }

    AutoElement rect("rect", fWriter, fResourceBucket.get(), MxCp(this), paint);

    if (svg) {
      rect.addAttribute("x", 0);
      rect.addAttribute("y", 0);
      rect.addAttribute("width", "100%");
      rect.addAttribute("height", "100%");
    } else {
      rect.addRectAttributes(r);
    }
}

void SkSVGDevice::drawOval(const SkRect& oval, const SkPaint& paint) {
    AutoElement ellipse("ellipse", fWriter, fResourceBucket.get(), MxCp(this), paint);
    ellipse.addAttribute("cx", oval.centerX());
    ellipse.addAttribute("cy", oval.centerY());
    ellipse.addAttribute("rx", oval.width() / 2);
    ellipse.addAttribute("ry", oval.height() / 2);
}

void SkSVGDevice::drawRRect(const SkRRect& rr, const SkPaint& paint) {
    SkPath path;
    path.addRRect(rr);

    AutoElement elem("path", fWriter, fResourceBucket.get(), MxCp(this), paint);
    elem.addPathAttributes(path);
}

void SkSVGDevice::drawPath(const SkPath& path, const SkPaint& paint, bool pathIsMutable) {
    AutoElement elem("path", fWriter, fResourceBucket.get(), MxCp(this), paint);
    elem.addPathAttributes(path);

    // TODO: inverse fill types?
    if (path.getFillType() == SkPath::kEvenOdd_FillType) {
        elem.addAttribute("fill-rule", "evenodd");
    }
}

static sk_sp<SkData> encode(const SkBitmap& src) {
    SkDynamicMemoryWStream buf;
    return SkEncodeImage(&buf, src, SkEncodedImageFormat::kPNG, 80) ? buf.detachAsData() : nullptr;
}

void SkSVGDevice::drawBitmapCommon(const MxCp& mc, const SkBitmap& bm, const SkPaint& paint) {
    sk_sp<SkData> pngData = encode(bm);
    if (!pngData) {
        return;
    }

    size_t b64Size = SkBase64::Encode(pngData->data(), pngData->size(), nullptr);
    SkAutoTMalloc<char> b64Data(b64Size);
    SkBase64::Encode(pngData->data(), pngData->size(), b64Data.get());

    SkString svgImageData("data:image/png;base64,");
    svgImageData.append(b64Data.get(), b64Size);

    SkString imageID = fResourceBucket->addImage();
    {
        AutoElement defs("defs", fWriter);
        {
            AutoElement image("image", fWriter);
            image.addAttribute("id", imageID);
            image.addAttribute("width", bm.width());
            image.addAttribute("height", bm.height());
            image.addAttribute("xlink:href", svgImageData);
        }
    }

    {
        AutoElement imageUse("use", fWriter, fResourceBucket.get(), mc, paint);
        imageUse.addAttribute("xlink:href", SkStringPrintf("#%s", imageID.c_str()));
    }
}

void SkSVGDevice::drawBitmap(const SkBitmap& bitmap, SkScalar x, SkScalar y,
                             const SkPaint& paint) {
    MxCp mc(this);
    SkMatrix adjustedMatrix = *mc.fMatrix;
    adjustedMatrix.preTranslate(x, y);
    mc.fMatrix = &adjustedMatrix;

    drawBitmapCommon(mc, bitmap, paint);
}

void SkSVGDevice::drawSprite(const SkBitmap& bitmap,
                             int x, int y, const SkPaint& paint) {
    MxCp mc(this);
    SkMatrix adjustedMatrix = *mc.fMatrix;
    adjustedMatrix.preTranslate(SkIntToScalar(x), SkIntToScalar(y));
    mc.fMatrix = &adjustedMatrix;

    drawBitmapCommon(mc, bitmap, paint);
}

void SkSVGDevice::drawBitmapRect(const SkBitmap& bm, const SkRect* srcOrNull,
                                 const SkRect& dst, const SkPaint& paint,
                                 SkCanvas::SrcRectConstraint) {
    SkClipStack* cs = &this->cs();
    SkClipStack::AutoRestore ar(cs, false);
    if (srcOrNull && *srcOrNull != SkRect::Make(bm.bounds())) {
        cs->save();
        cs->clipRect(dst, this->ctm(), kIntersect_SkClipOp, paint.isAntiAlias());
    }

    SkMatrix adjustedMatrix;
    adjustedMatrix.setRectToRect(srcOrNull ? *srcOrNull : SkRect::Make(bm.bounds()),
                                 dst,
                                 SkMatrix::kFill_ScaleToFit);
    adjustedMatrix.postConcat(this->ctm());

    drawBitmapCommon(MxCp(&adjustedMatrix, cs), bm, paint);
}

void SkSVGDevice::drawPosText(const void* text, size_t len,
                              const SkScalar pos[], int scalarsPerPos, const SkPoint& offset,
                              const SkPaint& paint) {
    SkASSERT(scalarsPerPos == 1 || scalarsPerPos == 2);

    AutoElement elem("text", fWriter, fResourceBucket.get(), MxCp(this), paint);
    elem.addTextAttributes(paint);

    SVGTextBuilder builder(text, len, paint, offset, scalarsPerPos, pos);
    elem.addAttribute("x", builder.posX());
    elem.addAttribute("y", builder.posY());
    elem.addText(builder.text());
}

void SkSVGDevice::drawTextOnPath(const void* text, size_t len, const SkPath& path,
                                 const SkMatrix* matrix, const SkPaint& paint) {
    SkString pathID = fResourceBucket->addPath();

    {
        AutoElement defs("defs", fWriter);
        AutoElement pathElement("path", fWriter);
        pathElement.addAttribute("id", pathID);
        pathElement.addPathAttributes(path);

    }

    {
        AutoElement textElement("text", fWriter);
        textElement.addTextAttributes(paint);

        if (matrix && !matrix->isIdentity()) {
            textElement.addAttribute("transform", svg_transform(*matrix));
        }

        {
            AutoElement textPathElement("textPath", fWriter);
            textPathElement.addAttribute("xlink:href", SkStringPrintf("#%s", pathID.c_str()));

            if (paint.getTextAlign() != SkPaint::kLeft_Align) {
                SkASSERT(paint.getTextAlign() == SkPaint::kCenter_Align ||
                         paint.getTextAlign() == SkPaint::kRight_Align);
                textPathElement.addAttribute("startOffset",
                    paint.getTextAlign() == SkPaint::kCenter_Align ? "50%" : "100%");
            }

            SVGTextBuilder builder(text, len, paint, SkPoint::Make(0, 0), 0);
            textPathElement.addText(builder.text());
        }
    }
}

void SkSVGDevice::drawVertices(const SkVertices*, const SkVertices::Bone[], int, SkBlendMode,
                               const SkPaint&) {
    // todo
}

void SkSVGDevice::drawDevice(SkBaseDevice*, int x, int y,
                             const SkPaint&) {
    // todo
}
