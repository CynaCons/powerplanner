#include "pch.h"
#include "PptRenderer.h"

static void ApplyText(PowerPoint::ShapePtr sh, const Prim& p) {
	if (p.text.empty()) return;
	PowerPoint::TextFramePtr tf = sh->GetTextFrame();
	PowerPoint::TextRangePtr tr = tf->GetTextRange();
	tr->PutText(_bstr_t(p.text.c_str()));
	PowerPoint::FontPtr font = tr->GetFont();
	font->PutSize(p.style.fontSize);
	font->PutBold(p.style.bold ? Office::msoTrue : Office::msoFalse);
	font->GetColor()->PutPpRGB((Office::MsoRGBType)p.style.textBgr);
	long a = (p.style.align == TextAlign::Center) ? 2 : (p.style.align == TextAlign::Right) ? 3 : 1;
	tr->GetParagraphFormat()->PutAlignment((PowerPoint::PpParagraphAlignment)a);
	tf->PutVerticalAnchor(Office::msoAnchorMiddle);
	tf->PutWordWrap(Office::msoFalse);
}

std::vector<PowerPoint::ShapePtr> RenderScene(PowerPoint::ShapesPtr shapes, const Scene& sc) {
	std::vector<PowerPoint::ShapePtr> out;
	for (const auto& p : sc.prims) {
		PowerPoint::ShapePtr sh;
		switch (p.kind) {
		case PrimKind::Rect:      sh = shapes->AddShape(Office::msoShapeRectangle, p.x, p.y, p.w, p.h); break;
		case PrimKind::RoundRect: sh = shapes->AddShape(Office::msoShapeRoundedRectangle, p.x, p.y, p.w, p.h); break;
		case PrimKind::Diamond:   sh = shapes->AddShape(Office::msoShapeDiamond, p.x, p.y, p.w, p.h); break;
		case PrimKind::Line:      sh = shapes->AddConnector(Office::msoConnectorStraight, p.x, p.y, p.x2, p.y2); break;
		case PrimKind::Connector: sh = shapes->AddConnector(Office::msoConnectorElbow, p.x, p.y, p.x2, p.y2); break;
		case PrimKind::Text:      sh = shapes->AddTextbox(Office::msoTextOrientationHorizontal, p.x, p.y, p.w, p.h); break;
		}
		if (!sh) continue;
		sh->GetShadow()->PutVisible(Office::msoFalse);  // flat, Material-style
		const Style& s = p.style;

		if (p.kind == PrimKind::Line || p.kind == PrimKind::Connector) {
			PowerPoint::LineFormatPtr lf = sh->GetLine();
			lf->GetForeColor()->PutPpRGB((Office::MsoRGBType)s.lineBgr);
			lf->PutWeight(s.lineWeight);
			if (s.arrowEnd) lf->PutEndArrowheadStyle(Office::msoArrowheadTriangle);
		} else {
			if (s.fill) sh->GetFill()->GetForeColor()->PutPpRGB((Office::MsoRGBType)s.fillBgr);
			else        sh->GetFill()->PutVisible(Office::msoFalse);
			if (s.line) { sh->GetLine()->GetForeColor()->PutPpRGB((Office::MsoRGBType)s.lineBgr); sh->GetLine()->PutWeight(s.lineWeight); }
			else        sh->GetLine()->PutVisible(Office::msoFalse);
			ApplyText(sh, p);
		}

		if (!p.tagKind.empty()) sh->GetTags()->Add(_bstr_t("PP_KIND"), _bstr_t(p.tagKind.c_str()));
		if (!p.tagId.empty())   sh->GetTags()->Add(_bstr_t("PP_ID"), _bstr_t(p.tagId.c_str()));
		out.push_back(sh);
	}
	return out;
}
