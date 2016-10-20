// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

/**
 * @constructor
 * @extends {WebInspector.VBox}
 * @param {!WebInspector.NetworkLogView} networkLogView
 * @param {!WebInspector.SortableDataGrid} dataGrid
 */
WebInspector.NetworkTimelineColumn = function(networkLogView, dataGrid)
{
    WebInspector.VBox.call(this, true);
    this.registerRequiredCSS("network/networkTimelineColumn.css");

    this._canvas = this.contentElement.createChild("canvas");
    this._canvas.tabIndex = 1;
    this.setDefaultFocusedElement(this._canvas);

    /** @const */
    this._leftPadding = 5;
    /** @const */
    this._rightPadding = 5;

    this._dataGrid = dataGrid;
    this._networkLogView = networkLogView;

    this._vScrollElement = this.contentElement.createChild("div", "network-timeline-v-scroll");
    this._vScrollContent = this._vScrollElement.createChild("div");
    this._vScrollElement.addEventListener("scroll", this._onScroll.bind(this), { passive: true });
    this._vScrollElement.addEventListener("mousewheel", this._onMouseWheel.bind(this), { passive: true });
    this._canvas.addEventListener("mousewheel", this._onMouseWheel.bind(this), { passive: true });
    this._canvas.addEventListener("mousemove", this._onMouseMove.bind(this), true);
    this._canvas.addEventListener("mouseleave", this.setHoveredRequest.bind(this, null), true);

    this._dataGridScrollContainer = this._dataGrid.scrollContainer;
    this._dataGridScrollContainer.addEventListener("mousewheel", event => {
        event.consume(true);
        this._onMouseWheel(event);
    }, true);

    // TODO(allada) When timeline canvas moves out of experiment move this to stylesheet.
    this._dataGridScrollContainer.style.overflow = "hidden";
    this._dataGrid.setScrollContainer(this._vScrollElement);

    this._dataGrid.addEventListener(WebInspector.ViewportDataGrid.Events.ViewportCalculated, this._update.bind(this));
    this._dataGrid.addEventListener(WebInspector.DataGrid.Events.PaddingChanged, this._updateHeight.bind(this));

    /** @type {!Array<!WebInspector.NetworkRequest>} */
    this._requestData = [];

    /** @type {?WebInspector.NetworkRequest} */
    this._hoveredRequest = null;

    this._rowStripeColor = WebInspector.themeSupport.patchColor("#f5f5f5", WebInspector.ThemeSupport.ColorUsage.Background);
    this._rowHoverColor = WebInspector.themeSupport.patchColor("#ebf2fc", WebInspector.ThemeSupport.ColorUsage.Background);
}

WebInspector.NetworkTimelineColumn.Events = {
    RequestHovered: Symbol("RequestHovered")
}

WebInspector.NetworkTimelineColumn.prototype = {
    wasShown: function()
    {
        this.scheduleUpdate();
    },

    scheduleRefreshData: function()
    {
        this._needsRefreshData = true;
    },

    _refreshDataIfNeeded: function()
    {
        if (!this._needsRefreshData)
            return;
        this._needsRefreshData = false;
        var currentNode = this._dataGrid.rootNode();
        this._requestData = [];
        while (currentNode = currentNode.traverseNextNode(true))
            this._requestData.push(currentNode.request());
    },

    /**
     * @param {?WebInspector.NetworkRequest} request
     */
    setHoveredRequest: function(request)
    {
        this._hoveredRequest = request;
        this.scheduleUpdate();
    },

    /**
     * @param {!Event} event
     */
    _onMouseMove: function(event)
    {
        var request = this._getRequestFromPoint(event.offsetX, event.offsetY);
        this.dispatchEventToListeners(WebInspector.NetworkTimelineColumn.Events.RequestHovered, request);
    },

    /**
     * @param {!Event} event
     */
    _onMouseWheel: function(event)
    {
        this._vScrollElement.scrollTop -= event.wheelDeltaY;
        this._dataGridScrollContainer.scrollTop = this._vScrollElement.scrollTop;
        var request = this._getRequestFromPoint(event.offsetX, event.offsetY);
        this.dispatchEventToListeners(WebInspector.NetworkTimelineColumn.Events.RequestHovered, request);
    },

    /**
     * @param {!Event} event
     */
    _onScroll: function(event)
    {
        this._dataGridScrollContainer.scrollTop = this._vScrollElement.scrollTop;
    },

    /**
     * @param {number} x
     * @param {number} y
     * @return {?WebInspector.NetworkRequest}
     */
    _getRequestFromPoint: function(x, y)
    {
        var rowHeight = this._networkLogView.rowHeight();
        var scrollTop = this._vScrollElement.scrollTop;
        return this._requestData[Math.floor((scrollTop + y - this._networkLogView.headerHeight()) / rowHeight)] || null;
    },

    scheduleUpdate: function()
    {
        if (this._updateRequestID)
            return;
        this._updateRequestID = this.element.window().requestAnimationFrame(this._update.bind(this));
    },

    _update: function()
    {
        this.element.window().cancelAnimationFrame(this._updateRequestID);
        this._updateRequestID = null;

        this._refreshDataIfNeeded();

        this._startTime = this._networkLogView.calculator().minimumBoundary();
        this._endTime = this._networkLogView.calculator().maximumBoundary();
        this._resetCanvas();
        this._draw();
    },

    _updateHeight: function()
    {
        var totalHeight = this._dataGridScrollContainer.scrollHeight;
        this._vScrollContent.style.height = totalHeight + "px";
    },

    _resetCanvas: function()
    {
        var ratio = window.devicePixelRatio;
        this._canvas.width = this._offsetWidth * ratio;
        this._canvas.height = this._offsetHeight * ratio;
        this._canvas.style.width = this._offsetWidth + "px";
        this._canvas.style.height = this._offsetHeight + "px";
    },

    /**
     * @override
     */
    onResize: function()
    {
        WebInspector.VBox.prototype.onResize.call(this);
        this._offsetWidth = this.contentElement.offsetWidth;
        this._offsetHeight = this.contentElement.offsetHeight;
        this.scheduleUpdate();
    },

    /**
     * @param {!WebInspector.RequestTimeRangeNames} type
     * @return {string}
     */
    _colorForType: function(type)
    {
        var types = WebInspector.RequestTimeRangeNames;
        switch (type) {
        case types.Receiving:
        case types.ReceivingPush:
            return "#03A9F4";
        case types.Waiting:
            return "#00C853";
        case types.Connecting:
            return "#FF9800";
        case types.SSL:
            return "#9C27B0";
        case types.DNS:
            return "#009688";
        case types.Proxy:
            return "#A1887F";
        case types.Blocking:
            return "#AAAAAA";
        case types.Push:
            return "#8CDBff";
        case types.Queueing:
            return "white";
        case types.ServiceWorker:
        case types.ServiceWorkerPreparation:
        default:
            return "orange";
        }
    },

    /**
     * @param {number} time
     * @return {number}
     */
    _timeToPosition: function(time)
    {
        var availableWidth = this._offsetWidth - this._leftPadding - this._rightPadding;
        var timeToPixel = availableWidth / (this._endTime - this._startTime);
        return Math.floor(this._leftPadding + (time - this._startTime) * timeToPixel);
    },

    _draw: function()
    {
        var requests = this._requestData;
        var context = this._canvas.getContext("2d");
        context.save();
        context.scale(window.devicePixelRatio, window.devicePixelRatio);
        context.translate(0, this._networkLogView.headerHeight());
        context.rect(0, 0, this._offsetWidth, this._offsetHeight);
        context.clip();
        var rowHeight = this._networkLogView.rowHeight();
        var scrollTop = this._vScrollElement.scrollTop;
        var firstRequestIndex = Math.floor(scrollTop / rowHeight);
        var lastRequestIndex = Math.min(requests.length, firstRequestIndex + Math.ceil(this._offsetHeight / rowHeight));
        for (var i = firstRequestIndex; i < lastRequestIndex; i++) {
            var rowOffset = rowHeight * i;
            var request = requests[i];
            this._decorateRow(context, request, i, rowOffset - scrollTop, rowHeight);
            var ranges = WebInspector.RequestTimingView.calculateRequestTimeRanges(request, 0);
            for (var range of ranges) {
                if (range.name === WebInspector.RequestTimeRangeNames.Total ||
                    range.name === WebInspector.RequestTimeRangeNames.Sending ||
                    range.end - range.start === 0)
                    continue;
                this._drawBar(context, range, rowOffset - scrollTop);
            }
        }
        context.restore();
        this._drawDividers(context);
    },

    _drawDividers: function(context)
    {
        context.save();
        /** @const */
        var minGridSlicePx = 64; // minimal distance between grid lines.
        /** @const */
        var fontSize = 10;

        var drawableWidth = this._offsetWidth - this._leftPadding - this._rightPadding;
        var timelineDuration = this._timelineDuration();
        var dividersCount = drawableWidth / minGridSlicePx;
        var gridSliceTime = timelineDuration / dividersCount;
        var pixelsPerTime = drawableWidth / timelineDuration;

        // Align gridSliceTime to a nearest round value.
        // We allow spans that fit into the formula: span = (1|2|5)x10^n,
        // e.g.: ...  .1  .2  .5  1  2  5  10  20  50  ...
        // After a span has been chosen make grid lines at multiples of the span.

        var logGridSliceTime = Math.ceil(Math.log(gridSliceTime) / Math.LN10);
        gridSliceTime = Math.pow(10, logGridSliceTime);
        if (gridSliceTime * pixelsPerTime >= 5 * minGridSlicePx)
            gridSliceTime = gridSliceTime / 5;
        if (gridSliceTime * pixelsPerTime >= 2 * minGridSlicePx)
            gridSliceTime = gridSliceTime / 2;

        context.lineWidth = 1;
        context.strokeStyle = "rgba(0, 0, 0, .1)";
        context.font = fontSize + "px sans-serif";
        context.fillStyle = "#444"
        gridSliceTime = gridSliceTime;
        for (var position = gridSliceTime * pixelsPerTime; position < drawableWidth; position += gridSliceTime * pixelsPerTime) {
            // Added .5 because canvas drawing points are between pixels.
            var drawPosition = Math.floor(position) + this._leftPadding + .5;
            context.beginPath();
            context.moveTo(drawPosition, 0);
            context.lineTo(drawPosition, this._offsetHeight);
            context.stroke();
            if (position <= gridSliceTime * pixelsPerTime)
                continue;
            var textData = Number.secondsToString(position / pixelsPerTime);
            context.fillText(textData, drawPosition - context.measureText(textData).width - 2, Math.floor(this._networkLogView.headerHeight() - fontSize / 2));
        }
        context.restore();
    },

    /**
     * @return {number}
     */
    _timelineDuration: function()
    {
        return this._networkLogView.calculator().maximumBoundary() - this._networkLogView.calculator().minimumBoundary();
    },

    /**
     * @param {!WebInspector.RequestTimeRangeNames} type
     * @return {number}
     */
    _getBarHeight: function(type)
    {
        var types = WebInspector.RequestTimeRangeNames;
        switch (type) {
        case types.Connecting:
        case types.SSL:
        case types.DNS:
        case types.Proxy:
        case types.Blocking:
        case types.Push:
        case types.Queueing:
            return 7;
        default:
            return 13;
        }
    },

    /**
     * @param {!CanvasRenderingContext2D} context
     * @param {!WebInspector.RequestTimeRange} range
     * @param {number} y
     */
    _drawBar: function(context, range, y)
    {
        context.save();
        context.beginPath();
        var lineWidth = 0;
        var color = this._colorForType(range.name);
        var borderColor = color;
        if (range.name === WebInspector.RequestTimeRangeNames.Queueing) {
            borderColor = "lightgrey";
            lineWidth = 2;
        }
        if (range.name === WebInspector.RequestTimeRangeNames.Receiving)
            lineWidth = 2;
        context.fillStyle = color;
        var height = this._getBarHeight(range.name);
        y += Math.floor(this._networkLogView.rowHeight() / 2 - height / 2) + lineWidth / 2;
        var start = this._timeToPosition(range.start);
        var end = this._timeToPosition(range.end);
        context.rect(start, y, end - start, height - lineWidth);
        if (lineWidth) {
            context.lineWidth = lineWidth;
            context.strokeStyle = borderColor;
            context.stroke();
        }
        context.fill();
        context.restore();
    },

    /**
     * @param {!CanvasRenderingContext2D} context
     * @param {!WebInspector.NetworkRequest} request
     * @param {number} rowNumber
     * @param {number} y
     * @param {number} rowHeight
     */
    _decorateRow: function(context, request, rowNumber, y, rowHeight)
    {
        if (rowNumber % 2 === 1 && this._hoveredRequest !== request)
            return;
        context.save();
        context.beginPath();
        var color = this._rowStripeColor;
        if (this._hoveredRequest === request)
            color = this._rowHoverColor;

        context.fillStyle = color;
        context.rect(0, y, this._offsetWidth, rowHeight);
        context.fill();
        context.restore();
    },

    __proto__: WebInspector.VBox.prototype
}
