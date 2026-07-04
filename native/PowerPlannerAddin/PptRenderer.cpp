#include "pch.h"
#include "PptRenderer.h"

static void ApplyText(PowerPoint::ShapePtr sh, const Prim& p) {
	if (p.text.empty()) return;
	PowerPoint::TextFramePtr tf = sh->GetTextFrame();
	tf->PutMarginLeft(0.0f);
	tf->PutMarginRight(0.0f);
	tf->PutMarginTop(0.0f);
	tf->PutMarginBottom(0.0f);
	PowerPoint::TextRangePtr tr = tf->GetTextRange();
	tr->PutText(_bstr_t(p.text.c_str()));
	PowerPoint::FontPtr font = tr->GetFont();
	font->PutSize(p.style.fontSize);
	font->PutBold(p.style.bold ? Office::msoTrue : Office::msoFalse);
	font->GetColor()->PutPpRGB((Office::MsoRGBType)p.style.textBgr);
	font->PutName(_bstr_t(L"Segoe UI"));
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

		// Rounded-rectangle corner radius (points). PowerPoint's roundrect
		// adjustment #1 is the radius as a fraction of the SHORTER side; clamp
		// to [0, 0.5] (0.5 = fully stadium). corner==0 leaves PowerPoint's
		// default rounding untouched.
		if (p.kind == PrimKind::RoundRect && s.corner > 0.0f) {
			float shorter = (p.w < p.h ? p.w : p.h);
			if (shorter > 0.0f) {
				float adj = s.corner / shorter;
				if (adj < 0.0f) adj = 0.0f; else if (adj > 0.5f) adj = 0.5f;
				try { sh->GetAdjustments()->PutItem(1, adj); } catch (...) {}
			}
		}

		if (p.kind == PrimKind::Line || p.kind == PrimKind::Connector) {
			PowerPoint::LineFormatPtr lf = sh->GetLine();
			lf->GetForeColor()->PutPpRGB((Office::MsoRGBType)s.lineBgr);
			lf->PutWeight(s.lineWeight);
			if (s.arrowEnd) lf->PutEndArrowheadStyle(Office::msoArrowheadTriangle);
			if (s.dash || p.tagKind == "DEADLINE" || p.tagKind == "TODAY_LINE") {
				lf->PutDashStyle(s.dash ? Office::msoLineDashDot : Office::msoLineDash);
			}
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
