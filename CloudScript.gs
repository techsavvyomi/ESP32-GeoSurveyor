function doGet(e) {
  return ContentService
    .createTextOutput("GeoSurveyor API is running")
    .setMimeType(ContentService.MimeType.TEXT);
}

function doPost(e) {
  try {
    if (!e || !e.postData || !e.postData.contents) {
      return jsonResponse("error", "No POST data received");
    }

    var data = JSON.parse(e.postData.contents);

    var ss = SpreadsheetApp.openById("Enter Sheet ID");
    var sheet = ss.getSheetByName("Sheet1"); // exact tab name

    var timestamp = new Date();

    sheet.appendRow([
      timestamp,
      data.distance,
      data.latitude,
      data.longitude
    ]);

    return jsonResponse("success", "Data logged successfully");

  } catch (error) {
    return jsonResponse("error", error.toString());
  }
}

function jsonResponse(status, message) {
  return ContentService
    .createTextOutput(JSON.stringify({status:status, message:message}))
    .setMimeType(ContentService.MimeType.JSON);
}
