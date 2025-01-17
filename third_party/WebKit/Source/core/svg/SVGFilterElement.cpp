/*
 * Copyright (C) 2004, 2005, 2006, 2007 Nikolas Zimmermann <zimmermann@kde.org>
 * Copyright (C) 2004, 2005, 2006 Rob Buis <buis@kde.org>
 * Copyright (C) 2006 Samuel Weinig <sam.weinig@gmail.com>
 * Copyright (C) 2009 Dirk Schulze <krit@webkit.org>
 * Copyright (C) Research In Motion Limited 2010. All rights reserved.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public License
 * along with this library; see the file COPYING.LIB.  If not, write to
 * the Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 */

#include "core/svg/SVGFilterElement.h"

#include "core/frame/UseCounter.h"
#include "core/layout/svg/LayoutSVGResourceFilter.h"
#include "core/svg/SVGElementProxy.h"

namespace blink {

inline SVGFilterElement::SVGFilterElement(Document& document)
    : SVGElement(SVGNames::filterTag, document),
      SVGURIReference(this),
      m_x(SVGAnimatedLength::create(this,
                                    SVGNames::xAttr,
                                    SVGLength::create(SVGLengthMode::Width))),
      m_y(SVGAnimatedLength::create(this,
                                    SVGNames::yAttr,
                                    SVGLength::create(SVGLengthMode::Height))),
      m_width(
          SVGAnimatedLength::create(this,
                                    SVGNames::widthAttr,
                                    SVGLength::create(SVGLengthMode::Width))),
      m_height(
          SVGAnimatedLength::create(this,
                                    SVGNames::heightAttr,
                                    SVGLength::create(SVGLengthMode::Height))),
      m_filterUnits(SVGAnimatedEnumeration<SVGUnitTypes::SVGUnitType>::create(
          this,
          SVGNames::filterUnitsAttr,
          SVGUnitTypes::kSvgUnitTypeObjectboundingbox)),
      m_primitiveUnits(
          SVGAnimatedEnumeration<SVGUnitTypes::SVGUnitType>::create(
              this,
              SVGNames::primitiveUnitsAttr,
              SVGUnitTypes::kSvgUnitTypeUserspaceonuse)) {
  // Spec: If the x/y attribute is not specified, the effect is as if a value of
  // "-10%" were specified.
  m_x->setDefaultValueAsString("-10%");
  m_y->setDefaultValueAsString("-10%");
  // Spec: If the width/height attribute is not specified, the effect is as if a
  // value of "120%" were specified.
  m_width->setDefaultValueAsString("120%");
  m_height->setDefaultValueAsString("120%");

  addToPropertyMap(m_x);
  addToPropertyMap(m_y);
  addToPropertyMap(m_width);
  addToPropertyMap(m_height);
  addToPropertyMap(m_filterUnits);
  addToPropertyMap(m_primitiveUnits);
}

SVGFilterElement::~SVGFilterElement() {}

DEFINE_NODE_FACTORY(SVGFilterElement)

DEFINE_TRACE(SVGFilterElement) {
  visitor->trace(m_x);
  visitor->trace(m_y);
  visitor->trace(m_width);
  visitor->trace(m_height);
  visitor->trace(m_filterUnits);
  visitor->trace(m_primitiveUnits);
  visitor->trace(m_elementProxySet);
  SVGElement::trace(visitor);
  SVGURIReference::trace(visitor);
}

void SVGFilterElement::svgAttributeChanged(const QualifiedName& attrName) {
  bool isXYWH = attrName == SVGNames::xAttr || attrName == SVGNames::yAttr ||
                attrName == SVGNames::widthAttr ||
                attrName == SVGNames::heightAttr;
  if (isXYWH)
    updateRelativeLengthsInformation();

  if (isXYWH || attrName == SVGNames::filterUnitsAttr ||
      attrName == SVGNames::primitiveUnitsAttr) {
    SVGElement::InvalidationGuard invalidationGuard(this);
    LayoutSVGResourceContainer* layoutObject =
        toLayoutSVGResourceContainer(this->layoutObject());
    if (layoutObject)
      layoutObject->invalidateCacheAndMarkForLayout();

    return;
  }

  SVGElement::svgAttributeChanged(attrName);
}

void SVGFilterElement::childrenChanged(const ChildrenChange& change) {
  SVGElement::childrenChanged(change);

  if (change.byParser)
    return;

  if (LayoutObject* object = layoutObject())
    object->setNeedsLayoutAndFullPaintInvalidation(
        LayoutInvalidationReason::ChildChanged);
}

LayoutObject* SVGFilterElement::createLayoutObject(const ComputedStyle&) {
  return new LayoutSVGResourceFilter(this);
}

bool SVGFilterElement::selfHasRelativeLengths() const {
  return m_x->currentValue()->isRelative() ||
         m_y->currentValue()->isRelative() ||
         m_width->currentValue()->isRelative() ||
         m_height->currentValue()->isRelative();
}

SVGElementProxySet& SVGFilterElement::elementProxySet() {
  if (!m_elementProxySet)
    m_elementProxySet = new SVGElementProxySet;
  return *m_elementProxySet;
}

}  // namespace blink
