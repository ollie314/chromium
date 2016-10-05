"use strict";

let mockShapeDetectionReady = define(
  'mockShapeDetection',
  ['third_party/WebKit/public/platform/modules/shapedetection/shapedetection.mojom',
   'mojo/public/js/bindings',
   'mojo/public/js/connection',
   'mojo/public/js/core',
   'content/public/renderer/frame_interfaces',
  ], (shapeDetection, bindings, connection, mojo, interfaces) => {

  class MockShapeDetection {
    constructor() {
      interfaces.addInterfaceOverrideForTesting(
          shapeDetection.ShapeDetection.name,
          pipe => this.bindToPipe(pipe));
    }

    bindToPipe(pipe) {
      this.stub_ = connection.bindHandleToStub(pipe,
                                               shapeDetection.ShapeDetection);
      bindings.StubBindings(this.stub_).delegate = this;
    }

    detectFace(frame_data, width, height) {
      let receivedStruct = mojo.mapBuffer(frame_data, 0, width*height*4, 0);
      this.buffer_data_ = new Uint32Array(receivedStruct.buffer);
      return Promise.resolve({
        result: {
          boundingBoxes: [
              { x : 1.0, y: 1.0, width: 100.0, height: 100.0 },
              { x : 2.0, y: 2.0, width: 200.0, height: 200.0 },
              { x : 3.0, y: 3.0, width: 300.0, height: 300.0 },
          ]
        }
      });
      mojo.unmapBuffer(receivedStruct.buffer);
    }

    getFrameData() {
      return this.buffer_data_;
    }
  }
  return new MockShapeDetection();
});
