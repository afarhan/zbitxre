// the Copy button on /upgrade: the one line, copied whole, so that it need
// not be typed; the author's tester typed only the address of it
"use strict";
(function () {
  var button = document.getElementById("copy");
  var line = document.getElementById("line");
  if (!button || !line || !navigator.clipboard) {
    if (button) { button.hidden = true; }
    return;
  }
  button.addEventListener("click", function () {
    navigator.clipboard.writeText(line.textContent).then(function () {
      button.textContent = "Copied";
      setTimeout(function () { button.textContent = "Copy"; }, 2000);
    });
  });
}());
