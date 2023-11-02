class WolfCheckbox extends HTMLElement {
    constructor() {
        super();
        var el = document.createElement("input");
        el.setAttribute("type", "checkbox");
        el.setAttribute("checked", this.getAttribute('checked')? this.getAttribute('checked'):"false");
        el.addEventListener('change', () => {
            window[this.getAttribute('onchange')](el.checked);
        })
        this.replaceWith(el)
    }

    disconnectedCallback() {
        this.removeEventListener('change');
    }
}

customElements.define("wolf-checkbox", WolfCheckbox);