{
    let resultBox = document.getElementById("strOut");
    let evType = "";
    function log(e) {
        let oldContent = resultBox.textContent;
        let addContent = "";
        for(const key in e) {
            addContent = addContent + key.toString()+": "+e[key].toString()+"\n";
        }
        resultBox.textContent = "==== ==== ("+evType+") ==== ====\n" + addContent + oldContent.substring(0,Math.min(4096,oldContent.length))
    }
    document.getElementById("strIn").addEventListener("keydown", (e)=>{evType="keydown";log(e)});
    document.getElementById("strIn").addEventListener("keypress", (e)=>{evType="keypress";log(e)});
    document.getElementById("strIn").addEventListener("keyup", (e)=>{evType="keyup";log(e)});
    document.getElementById("strIn").addEventListener("mousewheel", (e)=>{evType="mousewheel";log(e)});
}