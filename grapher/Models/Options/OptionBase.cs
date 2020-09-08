﻿using System;
using System.Collections.Generic;
using System.Linq;
using System.Text;
using System.Threading.Tasks;

namespace grapher.Models.Options
{
    public abstract class OptionBase : IOption
    {
        public abstract int Top { get; set; }

        public abstract int Height { get; }
        
        public abstract int Left { get; set; }

        public abstract int Width { get; set; }

        public int Beneath { 
            get
            {
                return Top + Height + Constants.OptionVerticalSeperation;
            }
        }

        public abstract bool Visible { get; }

        public abstract void Show(string Name);

        public abstract void Hide();

        public abstract void AlignActiveValues(int width);

        public virtual void SnapTo(IOption option)
        {
            Top = option.Beneath;
        }
    }
}
