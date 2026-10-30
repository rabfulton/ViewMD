### Headings

# h1 Heading

## h2 Heading

### h3 Heading

#### h4 Heading

##### h5 Heading

###### h6 Heading

### Links

[Link text](/)

[Link with title](/blog 'My blog!')

### Images

![Markdown logo](/uploads/Markdown-Logo.webp)
![Syki Logo](/logo512.png 'My logo')

### Lists

#### Unordered

-   Lorem ipsum dolor sit amet
-   Lorem ipsum dolor sit amet
    -   Lorem ipsum dolor sit amet
        -   Lorem ipsum dolor sit amet
        -   Lorem ipsum dolor sit amet
        -   Lorem ipsum dolor sit amet
-   Lorem ipsum dolor sit amet

#### Ordered

1. Lorem ipsum dolor sit amet
2. Lorem ipsum dolor sit amet
3. Lorem ipsum dolor sit amet

Start numbering with offset:

57. Lorem ipsum dolor sit amet
1. Lorem ipsum dolor sit amet

#### Checkboxes

-   [ ] Lorem ipsum dolor sit amet
-   [x] Lorem ipsum dolor sit amet
-   [ ] Lorem ipsum dolor sit amet

### Emphasis

**Bold text**

_Italic text_

~~Strikethrough~~

### Horizontal Rule

---

### Blockquotes

> Blockquotes
>
> > Nested blockquotes
> >
> > > Nested blockquotes

### Code

Inline `code`

```
Sample text here...
```

Syntax highlighting

```js
var foo = function (bar) {
    return bar++
}

console.log(foo(5))
```

### Tables

| Heading1 | Heading2                   |
| -------- | -------------------------- |
| row1     | Lorem ipsum dolor sit amet |
| row2     | Lorem ipsum dolor sit amet |
| row3     | Lorem ipsum dolor sit amet |

Right aligned columns

| Heading1 |                   Heading2 |
| -------: | -------------------------: |
|     row1 | Lorem **ipsum** dolor sit amet |
|     row2 | Lorem ipsum dolor sit amet |
|     row3 | Lorem ipsum `dolor` sit amet |

### HTML

This is inline <span style="color: red;">html</span>

<audio controls>
    <source src="/uploads/medium-drill-burst.mp3" type="audio`/mpeg" />
    Your browser does not support the audio element.
</audio>

### XSS Atack

<script>alert('XSS Atack. When you see this you should use sanitizer.')</script>
