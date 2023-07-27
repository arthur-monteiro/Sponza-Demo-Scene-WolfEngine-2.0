class WolfSelect extends HTMLElement {
    constructor() {
        super();
        var el = document.createElement("select");
        var children = this.childNodes;
        children.forEach(function(item){
            var cln = item.cloneNode(true);
            el.appendChild(cln);
        });
        el.addEventListener('change', () => {
            window[this.getAttribute('onchange')](el.value);
        })
        this.replaceWith(el)
    }

    disconnectedCallback() {
        this.removeEventListener('change');
    }
}

customElements.define("wolf-select", WolfSelect);